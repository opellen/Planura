#include <geo/journal.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/scene_ops.h>

#include <gtest/gtest.h>

namespace {

using plnr::geo::almostEqual;
using plnr::geo::closureOf;
using plnr::geo::copyInto;
using plnr::geo::Definition;
using plnr::geo::EntityKind;
using plnr::geo::EntitySet;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::kRootDefinitionId;
using plnr::geo::Model;
using plnr::geo::ModelJournal;
using plnr::geo::Scene;
using plnr::geo::SceneJournal;
using plnr::geo::Transform;
using plnr::geo::Vec3;

// =============================================================================
// Model-side: RecordingModelJournal + replay engine
// =============================================================================

enum class ModelOpKind {
  VertexCreated,
  VertexMoved,
  VertexDeleted,
  EdgeCreated,
  EdgeDeleted,
  FaceCreated,
  FaceDeleted,
  FaceLoopModified,
};

// One captured ModelJournal call, in emission order; which fields are meaningful depends on kind.
struct ModelOp {
  ModelOpKind kind;
  Id id{};
  Vec3 vecA{};
  Vec3 vecB{};
  Id idA{};
  Id idB{};
  std::vector<Id> loopA;
  std::vector<Id> loopB;
};

struct RecordingModelJournal : ModelJournal {
  std::vector<ModelOp> ops;

  void vertexCreated(Id id, Vec3 pos) override {
    ModelOp op;
    op.kind = ModelOpKind::VertexCreated;
    op.id = id;
    op.vecA = pos;
    ops.push_back(op);
  }
  void vertexMoved(Id id, Vec3 before, Vec3 after) override {
    ModelOp op;
    op.kind = ModelOpKind::VertexMoved;
    op.id = id;
    op.vecA = before;
    op.vecB = after;
    ops.push_back(op);
  }
  void vertexDeleted(Id id, Vec3 posBefore) override {
    ModelOp op;
    op.kind = ModelOpKind::VertexDeleted;
    op.id = id;
    op.vecA = posBefore;
    ops.push_back(op);
  }
  void edgeCreated(Id id, Id v0, Id v1) override {
    ModelOp op;
    op.kind = ModelOpKind::EdgeCreated;
    op.id = id;
    op.idA = v0;
    op.idB = v1;
    ops.push_back(op);
  }
  void edgeDeleted(Id id, Id v0, Id v1) override {
    ModelOp op;
    op.kind = ModelOpKind::EdgeDeleted;
    op.id = id;
    op.idA = v0;
    op.idB = v1;
    ops.push_back(op);
  }
  void faceCreated(Id id, const std::vector<Id> &loop) override {
    ModelOp op;
    op.kind = ModelOpKind::FaceCreated;
    op.id = id;
    op.loopA = loop;
    ops.push_back(op);
  }
  void faceDeleted(Id id, const std::vector<Id> &loopBefore) override {
    ModelOp op;
    op.kind = ModelOpKind::FaceDeleted;
    op.id = id;
    op.loopA = loopBefore;
    ops.push_back(op);
  }
  void faceLoopModified(Id id, const std::vector<Id> &loopBefore,
                        const std::vector<Id> &loopAfter) override {
    ModelOp op;
    op.kind = ModelOpKind::FaceLoopModified;
    op.id = id;
    op.loopA = loopBefore;
    op.loopB = loopAfter;
    ops.push_back(op);
  }
};

// Raw per-record dump in stored order; seed data for restoreVertex/Edge/Face and comparison data
// (normalized only at comparison time).
struct RawRecords {
  std::vector<std::pair<Id, Vec3>> vertices;
  std::vector<std::tuple<Id, Id, Id>> edges;         // id, v0, v1
  std::vector<std::pair<Id, std::vector<Id>>> faces; // id, loop
};

RawRecords dumpModel(const Model &m) {
  RawRecords r;
  for (const auto &[id, v] : m.vertices()) {
    r.vertices.emplace_back(id, v.pos);
  }
  std::sort(r.vertices.begin(), r.vertices.end(),
            [](auto &a, auto &b) { return a.first < b.first; });

  for (const auto &[id, e] : m.edges()) {
    const Id v0 = m.halfEdge(e.halfEdges[0])->origin;
    const Id v1 = m.halfEdge(e.halfEdges[1])->origin;
    r.edges.emplace_back(id, v0, v1);
  }
  std::sort(r.edges.begin(), r.edges.end(),
            [](auto &a, auto &b) { return std::get<0>(a) < std::get<0>(b); });

  for (const auto &[id, f] : m.faces()) {
    (void)f;
    r.faces.emplace_back(id, m.faceVertexLoop(id));
  }
  std::sort(r.faces.begin(), r.faces.end(),
            [](auto &a, auto &b) { return a.first < b.first; });

  return r;
}

// Rotates loop to start at its minimum-id vertex without touching winding (a flipped winding must still differ).
std::vector<Id> canonicalLoop(std::vector<Id> loop) {
  if (loop.empty()) {
    return loop;
  }
  const auto it = std::min_element(loop.begin(), loop.end());
  std::rotate(loop.begin(), it, loop.end());
  return loop;
}

Id maxIdInRecords(const RawRecords &r) {
  Id maxId = kInvalidId;
  for (const auto &[id, pos] : r.vertices) {
    (void)pos;
    maxId = std::max(maxId, id);
  }
  for (const auto &[id, v0, v1] : r.edges) {
    maxId = std::max({maxId, id, v0, v1});
  }
  for (const auto &[id, loop] : r.faces) {
    maxId = std::max(maxId, id);
    for (Id v : loop) {
      maxId = std::max(maxId, v);
    }
  }
  return maxId;
}

Id maxIdInOps(const std::vector<ModelOp> &ops) {
  Id maxId = kInvalidId;
  for (const auto &op : ops) {
    maxId = std::max({maxId, op.id, op.idA, op.idB});
    for (Id v : op.loopA) {
      maxId = std::max(maxId, v);
    }
    for (Id v : op.loopB) {
      maxId = std::max(maxId, v);
    }
  }
  return maxId;
}

// Feeds r into restoreVertex/Edge/Face in dependency order after reserving nextId; ASSERTs each succeeds
// (a failure means bad RawRecords/nextId bookkeeping, not the SUT).
void seedModel(Model &fresh, const RawRecords &r, Id nextId) {
  ASSERT_TRUE(fresh.restoreNextId(nextId));
  for (const auto &[id, pos] : r.vertices) {
    ASSERT_TRUE(fresh.restoreVertex(id, pos));
  }
  for (const auto &[id, v0, v1] : r.edges) {
    ASSERT_TRUE(fresh.restoreEdge(id, v0, v1));
  }
  for (const auto &[id, loop] : r.faces) {
    ASSERT_TRUE(fresh.restoreFace(id, loop));
  }
}

// Comparison is insensitive to edge direction.
void expectRecordsEqual(const RawRecords &a, const RawRecords &b) {
  ASSERT_EQ(a.vertices.size(), b.vertices.size());
  for (std::size_t i = 0; i < a.vertices.size(); ++i) {
    EXPECT_EQ(a.vertices[i].first, b.vertices[i].first);
    EXPECT_TRUE(almostEqual(a.vertices[i].second, b.vertices[i].second))
        << "vertex " << a.vertices[i].first << " position mismatch";
  }

  ASSERT_EQ(a.edges.size(), b.edges.size());
  for (std::size_t i = 0; i < a.edges.size(); ++i) {
    const auto &[aId, aV0, aV1] = a.edges[i];
    const auto &[bId, bV0, bV1] = b.edges[i];
    EXPECT_EQ(aId, bId);
    const std::pair<Id, Id> aPair{std::min(aV0, aV1), std::max(aV0, aV1)};
    const std::pair<Id, Id> bPair{std::min(bV0, bV1), std::max(bV0, bV1)};
    EXPECT_EQ(aPair, bPair) << "edge " << aId << " endpoints mismatch";
  }

  ASSERT_EQ(a.faces.size(), b.faces.size());
  for (std::size_t i = 0; i < a.faces.size(); ++i) {
    EXPECT_EQ(a.faces[i].first, b.faces[i].first);
    EXPECT_EQ(canonicalLoop(a.faces[i].second),
              canonicalLoop(b.faces[i].second))
        << "face " << a.faces[i].first << " loop mismatch";
  }
}

// Forward apply = redo: created -> restore*, deleted -> erase*ForRollback, modified -> AFTER image.
bool applyForward(Model &m, const ModelOp &op) {
  switch (op.kind) {
  case ModelOpKind::VertexCreated:
    return m.restoreVertex(op.id, op.vecA);
  case ModelOpKind::VertexMoved:
    return m.setVertexPosForRollback(op.id, op.vecB);
  case ModelOpKind::VertexDeleted:
    return m.eraseVertexForRollback(op.id);
  case ModelOpKind::EdgeCreated:
    return m.restoreEdge(op.id, op.idA, op.idB);
  case ModelOpKind::EdgeDeleted:
    return m.eraseEdgeForRollback(op.id);
  case ModelOpKind::FaceCreated:
    return m.restoreFace(op.id, op.loopA);
  case ModelOpKind::FaceDeleted:
    return m.eraseFaceForRollback(op.id);
  case ModelOpKind::FaceLoopModified:
    return m.eraseFaceForRollback(op.id) && m.restoreFace(op.id, op.loopB);
  }
  return false;
}

// Backward apply = undo: created -> erase*ForRollback, deleted -> restore*, modified -> BEFORE image.
bool applyBackward(Model &m, const ModelOp &op) {
  switch (op.kind) {
  case ModelOpKind::VertexCreated:
    return m.eraseVertexForRollback(op.id);
  case ModelOpKind::VertexMoved:
    return m.setVertexPosForRollback(op.id, op.vecA);
  case ModelOpKind::VertexDeleted:
    return m.restoreVertex(op.id, op.vecA);
  case ModelOpKind::EdgeCreated:
    return m.eraseEdgeForRollback(op.id);
  case ModelOpKind::EdgeDeleted:
    return m.restoreEdge(op.id, op.idA, op.idB);
  case ModelOpKind::FaceCreated:
    return m.eraseFaceForRollback(op.id);
  case ModelOpKind::FaceDeleted:
    return m.restoreFace(op.id, op.loopA);
  case ModelOpKind::FaceLoopModified:
    return m.eraseFaceForRollback(op.id) && m.restoreFace(op.id, op.loopA);
  }
  return false;
}

bool replayForward(Model &m, const std::vector<ModelOp> &ops) {
  for (const auto &op : ops) {
    if (!applyForward(m, op)) {
      return false;
    }
  }
  return true;
}

bool replayBackward(Model &m, const std::vector<ModelOp> &ops) {
  for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
    if (!applyBackward(m, *it)) {
      return false;
    }
  }
  return true;
}

// Core invariant: replaying the captured ops BACKWARD onto the post-state must reproduce preDump, and
// FORWARD onto preDump must reproduce the post-state (redo viability).
void expectJournalRoundTrip(const Model &live, const RawRecords &preDump,
                            const std::vector<ModelOp> &ops) {
  const RawRecords postDump = dumpModel(live);
  const Id nextId = std::max({maxIdInRecords(preDump), maxIdInRecords(postDump),
                              maxIdInOps(ops)}) +
                    1;

  Model backward;
  seedModel(backward, postDump, nextId);
  ASSERT_TRUE(replayBackward(backward, ops));
  expectRecordsEqual(dumpModel(backward), preDump);

  Model forward;
  seedModel(forward, preDump, nextId);
  ASSERT_TRUE(replayForward(forward, ops));
  expectRecordsEqual(dumpModel(forward), postDump);
}

// =============================================================================
// Scene-side: RecordingSceneJournal
// =============================================================================

struct SceneOp {
  enum class Kind {
    DefinitionCreated,
    DefinitionDeleted,
    InstanceCreated,
    InstanceDeleted,
    InstanceModified
  };
  Kind kind;
  Id parentDefId{};
  Id id{};
  Id definitionId{};
  Transform xfA;
  Transform xfB;
  std::string nameA;
  std::string nameB;
  bool isGroup{};
};

struct RecordingSceneJournal : SceneJournal {
  std::vector<SceneOp> ops;

  void definitionCreated(Id id, const std::string &name,
                         bool isGroup) override {
    SceneOp op;
    op.kind = SceneOp::Kind::DefinitionCreated;
    op.id = id;
    op.nameA = name;
    op.isGroup = isGroup;
    ops.push_back(op);
  }
  void definitionDeleted(Id id, const std::string &name,
                         bool isGroup) override {
    SceneOp op;
    op.kind = SceneOp::Kind::DefinitionDeleted;
    op.id = id;
    op.nameA = name;
    op.isGroup = isGroup;
    ops.push_back(op);
  }
  void instanceCreated(Id parentDefId, Id id, Id definitionId,
                       const Transform &transform,
                       const std::string &name) override {
    SceneOp op;
    op.kind = SceneOp::Kind::InstanceCreated;
    op.parentDefId = parentDefId;
    op.id = id;
    op.definitionId = definitionId;
    op.xfA = transform;
    op.nameA = name;
    ops.push_back(op);
  }
  void instanceDeleted(Id parentDefId, Id id, Id definitionId,
                       const Transform &transformBefore,
                       const std::string &nameBefore) override {
    SceneOp op;
    op.kind = SceneOp::Kind::InstanceDeleted;
    op.parentDefId = parentDefId;
    op.id = id;
    op.definitionId = definitionId;
    op.xfA = transformBefore;
    op.nameA = nameBefore;
    ops.push_back(op);
  }
  void instanceModified(Id parentDefId, Id id, const Transform &before,
                        const Transform &after, const std::string &nameBefore,
                        const std::string &nameAfter) override {
    SceneOp op;
    op.kind = SceneOp::Kind::InstanceModified;
    op.parentDefId = parentDefId;
    op.id = id;
    op.xfA = before;
    op.xfB = after;
    op.nameA = nameBefore;
    op.nameB = nameAfter;
    ops.push_back(op);
  }
};

// =============================================================================
// Sanity: no journal attached, detach, rollback primitives reject bad input
// =============================================================================

TEST(JournalTest, NoNotificationsWhenNoJournalAttached) {
  Model model; // journal_ defaults to nullptr -- never attached in this test
  const auto ab = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
  const auto bc = model.addEdge(Vec3{1.0, 0.0, 0.0}, Vec3{1.0, 1.0, 0.0});
  const auto closing = model.addEdge(Vec3{1.0, 1.0, 0.0}, Vec3{0.0, 0.0, 0.0});
  ASSERT_TRUE(ab.created);
  ASSERT_TRUE(bc.created);
  ASSERT_TRUE(closing.created);
  ASSERT_EQ(closing.newFaces.size(), 1u);
  // No crash: every choke point's `if (journal_)` guard is honored.

  Scene scene; // journal_ defaults to nullptr too
  const Id defId = scene.createDefinition("G", true);
  scene.addInstance(kRootDefinitionId, defId, Transform::identity(), "I");
  EXPECT_NE(defId, kInvalidId);
}

TEST(JournalTest, DetachStopsNotifications) {
  Model model;
  RecordingModelJournal journal;
  model.setJournal(&journal);

  model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
  ASSERT_FALSE(journal.ops.empty());
  const std::size_t countWhileAttached = journal.ops.size();

  model.setJournal(nullptr);
  model.addEdge(Vec3{5.0, 5.0, 0.0}, Vec3{6.0, 5.0, 0.0});
  EXPECT_EQ(journal.ops.size(), countWhileAttached); // nothing new recorded
}

TEST(JournalTest, ModelRollbackPrimitivesRejectBadInputWithoutMutation) {
  Model model;
  const auto ab = model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});
  model.addEdge(Vec3{2.0, 0.0, 0.0}, Vec3{2.0, 2.0, 0.0});
  const auto closing = model.addEdge(Vec3{2.0, 2.0, 0.0}, Vec3{0.0, 0.0, 0.0});
  ASSERT_EQ(closing.newFaces.size(), 1u);
  const Id faceId = closing.newFaces[0];
  const Id vA = model.findVertex(Vec3{0.0, 0.0, 0.0})->id;

  // Unknown ids.
  EXPECT_FALSE(model.eraseVertexForRollback(999999));
  EXPECT_FALSE(model.eraseEdgeForRollback(999999));
  EXPECT_FALSE(model.eraseFaceForRollback(999999));
  EXPECT_FALSE(model.setVertexPosForRollback(999999, Vec3{1.0, 1.0, 1.0}));

  // A vertex still owning outgoing half-edges can't be erased for rollback.
  EXPECT_FALSE(model.eraseVertexForRollback(vA));
  EXPECT_NE(model.vertex(vA), nullptr);

  // An edge still claimed by a face (either side) can't be erased for rollback.
  EXPECT_FALSE(model.eraseEdgeForRollback(ab.edge));
  EXPECT_NE(model.edge(ab.edge), nullptr);

  // A face id that is really a vertex/edge id (or unknown) is rejected too.
  EXPECT_FALSE(model.eraseFaceForRollback(vA));
  EXPECT_NE(model.face(faceId), nullptr);
}

TEST(JournalTest, SceneRollbackPrimitivesRejectBadInputWithoutMutation) {
  Scene scene;
  const Id defId = scene.createDefinition("G", true);
  const Id instId =
      scene.addInstance(kRootDefinitionId, defId, Transform::identity(), "I");

  // Root can never be erased for rollback.
  EXPECT_FALSE(scene.eraseDefinitionForRollback(kRootDefinitionId));
  // defId is still referenced by instId -- can't erase for rollback yet.
  EXPECT_FALSE(scene.eraseDefinitionForRollback(defId));
  EXPECT_NE(scene.definition(defId), nullptr);
  // Unknown definition id.
  EXPECT_FALSE(scene.eraseDefinitionForRollback(999999));

  // Unknown instance.
  EXPECT_FALSE(scene.eraseInstanceForRollback(kRootDefinitionId, 999999));
  EXPECT_FALSE(scene.setInstanceForRollback(kRootDefinitionId, 999999,
                                            Transform::identity(), "X"));
  EXPECT_NE(scene.findInstance(kRootDefinitionId, instId), nullptr);
}

// =============================================================================
// Per-mutator round-trip: addEdge
// =============================================================================

TEST(JournalTest, AddEdgeWithFaceDetectionRoundTrips) {
  Model model;
  model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});
  model.addEdge(Vec3{2.0, 0.0, 0.0}, Vec3{0.0, 2.0, 0.0});
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const auto closing = model.addEdge(
      Vec3{0.0, 2.0, 0.0}, Vec3{0.0, 0.0, 0.0}); // closes the triangle
  ASSERT_TRUE(closing.created);
  ASSERT_EQ(closing.newFaces.size(), 1u);
  model.setJournal(nullptr);

  // One new edge, one new face; no new vertices.
  bool sawEdgeCreated = false;
  bool sawFaceCreated = false;
  for (const auto &op : journal.ops) {
    EXPECT_NE(op.kind, ModelOpKind::VertexCreated);
    if (op.kind == ModelOpKind::EdgeCreated)
      sawEdgeCreated = true;
    if (op.kind == ModelOpKind::FaceCreated)
      sawFaceCreated = true;
  }
  EXPECT_TRUE(sawEdgeCreated);
  EXPECT_TRUE(sawFaceCreated);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

TEST(JournalTest, AddEdgeWithoutFaceDetectionRoundTrips) {
  Model model;
  model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0});
  model.addEdge(Vec3{2.0, 0.0, 0.0}, Vec3{0.0, 2.0, 0.0});
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const auto closing = model.addEdge(Vec3{0.0, 2.0, 0.0}, Vec3{0.0, 0.0, 0.0},
                                     /*detectFaces=*/false);
  ASSERT_TRUE(closing.created);
  ASSERT_TRUE(closing.newFaces.empty());
  model.setJournal(nullptr);

  for (const auto &op : journal.ops) {
    EXPECT_NE(op.kind, ModelOpKind::FaceCreated);
  }
  ASSERT_EQ(journal.ops.size(), 1u);
  EXPECT_EQ(journal.ops[0].kind, ModelOpKind::EdgeCreated);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

// =============================================================================
// addFaceOnLoop
// =============================================================================

TEST(JournalTest, AddFaceOnLoopRoundTrips) {
  Model model;
  model.addEdge(Vec3{0.0, 0.0, 0.0}, Vec3{2.0, 0.0, 0.0},
                /*detectFaces=*/false);
  model.addEdge(Vec3{2.0, 0.0, 0.0}, Vec3{2.0, 2.0, 0.0},
                /*detectFaces=*/false);
  model.addEdge(Vec3{2.0, 2.0, 0.0}, Vec3{0.0, 0.0, 0.0},
                /*detectFaces=*/false);
  const RawRecords preDump = dumpModel(model);

  const Id va = model.findVertex(Vec3{0.0, 0.0, 0.0})->id;
  const Id vb = model.findVertex(Vec3{2.0, 0.0, 0.0})->id;
  const Id vc = model.findVertex(Vec3{2.0, 2.0, 0.0})->id;

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const Id faceId = model.addFaceOnLoop({va, vb, vc}, Vec3{0.0, 0.0, 1.0});
  ASSERT_NE(faceId, kInvalidId);
  model.setJournal(nullptr);

  ASSERT_EQ(journal.ops.size(), 1u);
  EXPECT_EQ(journal.ops[0].kind, ModelOpKind::FaceCreated);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

// =============================================================================
// removeEdge: face-dissolve and orphan-vertex-GC, via two calls under one journal
// (one call can't trigger both).
// =============================================================================

TEST(JournalTest, RemoveEdgeFaceDissolveAndOrphanGCRoundTrips) {
  Model model;
  const Vec3 a{0.0, 0.0, 0.0};
  const Vec3 b{2.0, 0.0, 0.0};
  const Vec3 c{0.0, 2.0, 0.0};
  const Vec3 d{5.0, 5.0, 0.0}; // pendant, only ever connected via c-d

  const auto ab = model.addEdge(a, b);
  model.addEdge(b, c);
  const auto closing = model.addEdge(c, a);
  ASSERT_EQ(closing.newFaces.size(), 1u);
  const auto cd = model.addEdge(c, d);
  ASSERT_TRUE(cd.created);
  const Id vD = model.findVertex(d)->id;
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  ASSERT_TRUE(model.removeEdge(ab.edge)); // dissolves the triangle face
  ASSERT_TRUE(model.removeEdge(cd.edge)); // orphans d
  model.setJournal(nullptr);

  EXPECT_EQ(model.vertex(vD), nullptr); // d is gone (orphan-GC'd)
  bool sawFaceDeleted = false;
  bool sawVertexDeleted = false;
  for (const auto &op : journal.ops) {
    if (op.kind == ModelOpKind::FaceDeleted)
      sawFaceDeleted = true;
    if (op.kind == ModelOpKind::VertexDeleted)
      sawVertexDeleted = true;
  }
  EXPECT_TRUE(sawFaceDeleted);
  EXPECT_TRUE(sawVertexDeleted);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

// =============================================================================
// splitEdge: face re-chain (shared-edge, two-faces case) and the weld/
// vertex-reuse case
// =============================================================================

TEST(JournalTest, SplitEdgeSharedFaceRechainRoundTrips) {
  Model model;
  const Vec3 a{0.0, 0.0, 0.0};
  const Vec3 b{1.0, 0.0, 0.0};
  const Vec3 c{1.0, 1.0, 0.0};
  const Vec3 d{0.0, 1.0, 0.0};
  model.addEdge(a, b);
  model.addEdge(b, c);
  const auto diag = model.addEdge(c, a);
  ASSERT_EQ(diag.newFaces.size(), 1u);
  model.addEdge(c, d);
  const auto closing = model.addEdge(d, a);
  ASSERT_EQ(closing.newFaces.size(), 1u);
  ASSERT_EQ(model.faces().size(), 2u);
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const auto split = model.splitEdge(diag.edge, Vec3{0.5, 0.5, 0.0});
  ASSERT_TRUE(split.ok);
  model.setJournal(nullptr);

  // Both faces re-chain in place (faceLoopModified, never faceDeleted/faceCreated); original edge
  // deleted, two created, midpoint vertex created.
  int faceLoopModifiedCount = 0;
  for (const auto &op : journal.ops) {
    EXPECT_NE(op.kind, ModelOpKind::FaceDeleted);
    EXPECT_NE(op.kind, ModelOpKind::FaceCreated);
    if (op.kind == ModelOpKind::FaceLoopModified)
      ++faceLoopModifiedCount;
  }
  EXPECT_EQ(faceLoopModifiedCount, 2);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

TEST(JournalTest, SplitEdgeWeldReuseRoundTrips) {
  Model model;
  const auto e1 = model.addEdge(Vec3{-1.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0});
  const auto e2 = model.addEdge(Vec3{0.0, -1.0, 0.0}, Vec3{0.0, 1.0, 0.0});
  ASSERT_TRUE(e1.created);
  ASSERT_TRUE(e2.created);
  const Vec3 crossing{0.0, 0.0, 0.0};
  const auto firstSplit = model.splitEdge(e1.edge, crossing);
  ASSERT_TRUE(firstSplit.ok);
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const auto secondSplit =
      model.splitEdge(e2.edge, crossing); // reuses firstSplit's vertex
  ASSERT_TRUE(secondSplit.ok);
  EXPECT_EQ(secondSplit.newVertex, firstSplit.newVertex);
  model.setJournal(nullptr);

  // Reused vertex -- no vertexCreated for it.
  for (const auto &op : journal.ops) {
    EXPECT_NE(op.kind, ModelOpKind::VertexCreated);
  }

  expectJournalRoundTrip(model, preDump, journal.ops);
}

// =============================================================================
// splitFaceByChord
// =============================================================================

TEST(JournalTest, SplitFaceByChordRoundTrips) {
  Model model;
  const Vec3 a{0.0, 0.0, 0.0};
  const Vec3 b{4.0, 0.0, 0.0};
  const Vec3 c{4.0, 3.0, 0.0};
  const Vec3 d{0.0, 3.0, 0.0};
  model.addEdge(a, b);
  model.addEdge(b, c);
  model.addEdge(c, d);
  const auto closing = model.addEdge(d, a);
  ASSERT_EQ(closing.newFaces.size(), 1u);
  const Id faceId = closing.newFaces[0];
  const auto chord = model.addEdge(a, c, /*detectFaces=*/false);
  ASSERT_TRUE(chord.created);
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const auto split = model.splitFaceByChord(faceId, chord.edge);
  ASSERT_TRUE(split.ok);
  model.setJournal(nullptr);

  bool sawFaceDeleted = false;
  int faceCreatedCount = 0;
  for (const auto &op : journal.ops) {
    if (op.kind == ModelOpKind::FaceDeleted)
      sawFaceDeleted = true;
    if (op.kind == ModelOpKind::FaceCreated)
      ++faceCreatedCount;
  }
  EXPECT_TRUE(sawFaceDeleted);
  EXPECT_EQ(faceCreatedCount, 2);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

// =============================================================================
// extrudeFace, plain and the re-extrude-cap shared-edge case
// =============================================================================

Id buildRectangleFace(Model &model) {
  const Vec3 p0{0.0, 0.0, 0.0};
  const Vec3 p1{4.0, 0.0, 0.0};
  const Vec3 p2{4.0, 3.0, 0.0};
  const Vec3 p3{0.0, 3.0, 0.0};
  model.addEdge(p0, p1);
  model.addEdge(p1, p2);
  model.addEdge(p2, p3);
  const auto closing = model.addEdge(p3, p0);
  return closing.newFaces.empty() ? kInvalidId : closing.newFaces[0];
}

TEST(JournalTest, ExtrudeFaceRoundTrips) {
  Model model;
  const Id faceId = buildRectangleFace(model);
  ASSERT_NE(faceId, kInvalidId);
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const auto result = model.extrudeFace(faceId, 2.0);
  ASSERT_TRUE(result.ok);
  model.setJournal(nullptr);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

TEST(JournalTest, ExtrudeFaceReExtrudeCapSharedEdgeRoundTrips) {
  Model model;
  const Id faceId = buildRectangleFace(model);
  ASSERT_NE(faceId, kInvalidId);
  const auto first = model.extrudeFace(faceId, 2.0); // setup, no journal
  ASSERT_TRUE(first.ok);
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const auto second =
      model.extrudeFace(first.capFace, 1.0); // the shared-edge case
  ASSERT_TRUE(second.ok);
  model.setJournal(nullptr);

  expectJournalRoundTrip(model, preDump, journal.ops);
}

// =============================================================================
// translateVertices / transformVertices
// =============================================================================

TEST(JournalTest, TranslateVerticesRoundTrips) {
  Model model;
  const Id faceId = buildRectangleFace(model);
  ASSERT_NE(faceId, kInvalidId);
  const std::vector<Id> loop = model.faceVertexLoop(faceId);
  ASSERT_EQ(loop.size(), 4u);
  const Vec3 normalBefore = model.face(faceId)->normal;
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  ASSERT_TRUE(model.translateVertices({loop[0], loop[1]},
                                      Vec3{0.0, 0.0, 5.0})); // tilt the face
  model.setJournal(nullptr);

  ASSERT_EQ(journal.ops.size(), 2u);
  for (const auto &op : journal.ops) {
    EXPECT_EQ(op.kind, ModelOpKind::VertexMoved);
  }
  // Normal recompute is derived, not journaled: replay's setVertexPosForRollback recomputes it, so
  // backward replay restores it exactly.
  EXPECT_FALSE(almostEqual(model.face(faceId)->normal, normalBefore, 1e-6));

  expectJournalRoundTrip(model, preDump, journal.ops);
}

TEST(JournalTest, TransformVerticesRoundTrips) {
  Model model;
  const Id faceId = buildRectangleFace(model);
  ASSERT_NE(faceId, kInvalidId);
  const std::vector<Id> loop = model.faceVertexLoop(faceId);
  const RawRecords preDump = dumpModel(model);

  RecordingModelJournal journal;
  model.setJournal(&journal);
  const Transform xf =
      Transform::rotation(Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, 0.3);
  ASSERT_TRUE(model.transformVertices(loop, xf));
  model.setJournal(nullptr);

  ASSERT_EQ(journal.ops.size(), loop.size());
  for (const auto &op : journal.ops) {
    EXPECT_EQ(op.kind, ModelOpKind::VertexMoved);
  }

  expectJournalRoundTrip(model, preDump, journal.ops);
}

// =============================================================================
// Scene: createDefinition, addInstance, removeInstance
// =============================================================================

TEST(JournalTest, SceneCreateDefinitionRoundTrips) {
  Scene scene;
  RecordingSceneJournal journal;
  scene.setJournal(&journal);
  const Id defId = scene.createDefinition("Widget", /*isGroup=*/true);
  scene.setJournal(nullptr);
  ASSERT_NE(defId, kInvalidId);

  ASSERT_EQ(journal.ops.size(), 1u);
  const SceneOp &op = journal.ops[0];
  EXPECT_EQ(op.kind, SceneOp::Kind::DefinitionCreated);
  EXPECT_EQ(op.id, defId);
  EXPECT_EQ(op.nameA, "Widget");
  EXPECT_TRUE(op.isGroup);

  // Backward: eraseDefinitionForRollback reproduces the empty pre-state.
  EXPECT_TRUE(scene.eraseDefinitionForRollback(defId));
  EXPECT_EQ(scene.definition(defId), nullptr);

  // Forward: onto a fresh scene, restoreDefinition reproduces post-state.
  Scene fresh;
  ASSERT_TRUE(fresh.restoreNextId(defId + 1));
  Definition *restored = fresh.restoreDefinition(defId, op.nameA, op.isGroup);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->id, defId);
  EXPECT_EQ(restored->name, "Widget");
  EXPECT_TRUE(restored->isGroup);
}

TEST(JournalTest, SceneAddInstanceRoundTrips) {
  Scene scene;
  const Id defId = scene.createDefinition("Widget", true); // setup, no journal

  RecordingSceneJournal journal;
  scene.setJournal(&journal);
  const Transform xf = Transform::translation(Vec3{1.0, 2.0, 3.0});
  const Id instId = scene.addInstance(kRootDefinitionId, defId, xf, "Inst A");
  scene.setJournal(nullptr);
  ASSERT_NE(instId, kInvalidId);

  ASSERT_EQ(journal.ops.size(), 1u);
  const SceneOp &op = journal.ops[0];
  EXPECT_EQ(op.kind, SceneOp::Kind::InstanceCreated);
  EXPECT_EQ(op.parentDefId, kRootDefinitionId);
  EXPECT_EQ(op.id, instId);
  EXPECT_EQ(op.definitionId, defId);
  EXPECT_TRUE(op.xfA.almostEqual(xf));
  EXPECT_EQ(op.nameA, "Inst A");

  // Backward.
  EXPECT_TRUE(scene.eraseInstanceForRollback(kRootDefinitionId, instId));
  EXPECT_EQ(scene.findInstance(kRootDefinitionId, instId), nullptr);

  // Forward: fresh scene seeded with defId only; restoreInstance reproduces post-state.
  Scene fresh;
  ASSERT_TRUE(fresh.restoreNextId(std::max(defId, instId) + 1));
  ASSERT_NE(fresh.restoreDefinition(defId, "Widget", true), nullptr);
  ASSERT_TRUE(fresh.restoreInstance(op.parentDefId, op.id, op.definitionId,
                                    op.xfA, op.nameA));
  const auto *restoredInst = fresh.findInstance(kRootDefinitionId, instId);
  ASSERT_NE(restoredInst, nullptr);
  EXPECT_EQ(restoredInst->definitionId, defId);
  EXPECT_TRUE(restoredInst->transform.almostEqual(xf));
  EXPECT_EQ(restoredInst->name, "Inst A");
}

TEST(JournalTest, SceneRemoveInstanceRoundTrips) {
  Scene scene;
  const Id defId = scene.createDefinition("Widget", true);
  const Transform xf = Transform::translation(Vec3{1.0, 2.0, 3.0});
  const Id instId = scene.addInstance(kRootDefinitionId, defId, xf,
                                      "Inst A"); // setup, no journal

  RecordingSceneJournal journal;
  scene.setJournal(&journal);
  ASSERT_TRUE(scene.removeInstance(kRootDefinitionId, instId));
  scene.setJournal(nullptr);

  ASSERT_EQ(journal.ops.size(), 1u);
  const SceneOp &op = journal.ops[0];
  EXPECT_EQ(op.kind, SceneOp::Kind::InstanceDeleted);
  EXPECT_EQ(op.parentDefId, kRootDefinitionId);
  EXPECT_EQ(op.id, instId);
  EXPECT_EQ(op.definitionId, defId);
  EXPECT_TRUE(op.xfA.almostEqual(xf));
  EXPECT_EQ(op.nameA, "Inst A");

  // Backward: restoreInstance with the before-image reproduces pre-state.
  EXPECT_TRUE(scene.restoreInstance(op.parentDefId, op.id, op.definitionId,
                                    op.xfA, op.nameA));
  const auto *restored = scene.findInstance(kRootDefinitionId, instId);
  ASSERT_NE(restored, nullptr);
  EXPECT_TRUE(restored->transform.almostEqual(xf));

  // Forward (redo removal): eraseInstanceForRollback reproduces the empty post-state.
  EXPECT_TRUE(scene.eraseInstanceForRollback(kRootDefinitionId, instId));
  EXPECT_EQ(scene.findInstance(kRootDefinitionId, instId), nullptr);
}

// =============================================================================
// Composite: geo::copyInto with journals on both Models
// =============================================================================

TEST(JournalTest, CopyIntoRoundTripsOnDestinationAndNeverTouchesSource) {
  Model src;
  const Id srcFace = buildRectangleFace(src);
  ASSERT_NE(srcFace, kInvalidId);

  RecordingModelJournal srcJournal;
  src.setJournal(&srcJournal);

  Model dst;
  const RawRecords preDump = dumpModel(dst); // empty
  RecordingModelJournal dstJournal;
  dst.setJournal(&dstJournal);

  const EntitySet set = closureOf(src, {{EntityKind::Face, srcFace}});
  copyInto(dst, src, set, Transform::translation(Vec3{10.0, 0.0, 0.0}));

  dst.setJournal(nullptr);
  src.setJournal(nullptr);

  // copyInto never mutates src; its journal must stay silent.
  EXPECT_TRUE(srcJournal.ops.empty());

  // dst gained 4 vertices, 4 edges, 1 face via the ordinary choke points.
  ASSERT_FALSE(dstJournal.ops.empty());
  expectJournalRoundTrip(dst, preDump, dstJournal.ops);
}

} // namespace
