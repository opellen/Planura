#include "ui/context_menu.h"

#include <optional>
#include <string>
#include <vector>

#include <QPointF>

#include <ordo/core/kernel.h>

#include "agent/events.h"

#include <gtest/gtest.h>

namespace {

using ordo::core::Kernel;
using plnr::events::EraseGuideRequested;
using plnr::events::RemoveEdgeRequested;
using plnr::events::ResetAxesRequested;
using plnr::events::ReverseSectionRequested;
using plnr::events::SetSectionActiveRequested;
using plnr::geo::EntityKind;
using plnr::geo::GuideLineData;
using plnr::geo::GuidePointData;
using plnr::geo::Id;
using plnr::geo::kInvalidId;
using plnr::geo::PickKind;
using plnr::geo::PickOptions;
using plnr::geo::PickResult;
using plnr::geo::Ray;
using plnr::geo::Vec3;
using plnr::tools::AxesFrame;
using plnr::tools::PointerEvent;
using plnr::tools::SectionPlaneData;
using plnr::tools::ToolContext;
using plnr::ui::buildContextMenu;
using plnr::ui::ContextMenuItem;

// ToolContext double for buildContextMenu: only pick(), model(), guidePoints(),
// guideLines(), sections(), axesFrame() are configurable; the rest are stubs.
class FakeToolContext : public ToolContext {
public:
  PickResult pickResult;
  std::vector<GuidePointData> guidePointsData;
  std::vector<GuideLineData> guideLinesData;
  std::vector<SectionPlaneData> sectionsData;
  AxesFrame axesFrameData;
  Id frontMaterialOfData = 0;
  MaterialTextureInfo materialTextureInfoData;

  PickResult pick(const PointerEvent &, const PickOptions &) const override {
    return pickResult;
  }
  const plnr::geo::Model *model() const override { return nullptr; }
  std::vector<GuidePointData> guidePoints() const override {
    return guidePointsData;
  }
  std::vector<GuideLineData> guideLines() const override {
    return guideLinesData;
  }
  std::vector<SectionPlaneData> sections() const override {
    return sectionsData;
  }
  AxesFrame axesFrame() const override { return axesFrameData; }

  void requestAddEdge(Vec3, Vec3) override {}
  void requestAddRectangle(Vec3, Vec3) override {}
  void requestAddPolyline(const std::vector<Vec3> &, bool) override {}
  void requestReplaceLastPolyline(const std::vector<Vec3> &, bool) override {}
  void requestExtrudeFace(Id, double) override {}
  void requestMoveEntity(EntityKind, Id, Vec3) override {}
  void requestRemoveEdge(Id) override {}
  void requestTransformEntities(const std::vector<plnr::events::EntityRef> &,
                                const plnr::events::TransformSpec &,
                                int) override {}
  void requestApplyArrayTimes(int) override {}
  void requestApplyArrayDivide(int) override {}
  void requestFollowMe(Id, const std::vector<Vec3> &, bool) override {}
  void requestSelect(plnr::events::SelectMode,
                     std::optional<plnr::events::EntityRef>,
                     plnr::events::SelectExpand) override {}
  void requestSelectRegion(plnr::events::SelectMode, const std::array<Ray, 4> &,
                           bool) override {}
  void requestEnterContext(Id) override {}
  void requestExitContext() override {}
  bool atRootContext() const override { return true; }
  void requestAddGuideLine(Vec3, Vec3) override {}
  void requestAddGuidePoint(Vec3) override {}
  void requestEraseGuide(Id) override {}
  void requestDeleteAllGuides() override {}
  void requestSetAxes(Vec3, Vec3, Vec3) override {}
  void requestResetAxes() override {}
  void requestAddDimension(Id, Id, Vec3, double) override {}
  void requestAddScreenText(double, double, std::string) override {}
  void requestAddLeaderText(Vec3, std::optional<plnr::events::EntityRef>,
                            std::string) override {}
  void requestSetAnnotationText(Id, std::string) override {}
  void requestRemoveAnnotation(Id) override {}
  void requestAddSectionPlane(Vec3, Vec3, std::string) override {}
  void requestRemoveSectionPlane(Id) override {}
  void requestSetSectionActive(Id, bool) override {}
  void requestReverseSection(Id) override {}
  Id activeMaterialId() const override { return 0; }
  // Called by the face-hit branch; set via frontMaterialOfData.
  Id frontMaterialOf(plnr::events::EntityRef) const override {
    return frontMaterialOfData;
  }
  void requestPaint(const std::vector<plnr::events::EntityRef> &, Id) override {
  }
  void requestSetActiveMaterial(Id) override {}
  // Called by the face-hit branch; set via materialTextureInfoData.
  MaterialTextureInfo materialTextureInfo(Id) const override {
    return materialTextureInfoData;
  }
  plnr::events::UvTransform
  faceUvTransform(plnr::events::EntityRef) const override {
    return {};
  }
  void requestSetUvTransform(plnr::events::EntityRef,
                             plnr::events::UvTransform) override {}
  std::optional<std::string> promptText(std::string, std::string) override {
    return std::nullopt;
  }
  const std::vector<plnr::events::EntityRef> &selection() const override {
    static const std::vector<plnr::events::EntityRef> kEmpty;
    return kEmpty;
  }
  plnr::geo::ScenePickResult pickScene(const PointerEvent &,
                                       const PickOptions &) const override {
    return {};
  }
  Ray makeRay(QPointF) const override { return Ray{}; }
  void setPreview(std::vector<float>, std::optional<Vec3>) override {}
  void setPreviewBatches(std::vector<PreviewBatch>,
                         std::optional<Vec3>) override {}
  void setInferenceCue(std::optional<plnr::tools::InferenceCue>) override {}
  void setScreenRect(std::optional<QPointF>, std::optional<QPointF>) override {}
  void setHint(std::string) override {}
  void showWarning(std::string) override {}
  bool confirm(std::string) override { return false; }
  void setVcbLabel(std::string) override {}
  void setVcbValue(std::string) override {}
  void requestSolidOp(plnr::events::SolidOp, std::vector<Id>) override {}
  void requestToolChange(plnr::events::ToolId) override {}
  Id lastRootInstanceId() const override { return kInvalidId; }
  SolidTargetInfo solidTargetInfo(Id) const override { return {}; }
  bool isDefinitionSolid(Id) const override { return false; }
};

// Ray along +Z from (x,y,-5); targets sit at (x,y,0).
PointerEvent rayAt(double x, double y, double vertexTol = 0.05,
                   double edgeTol = 0.05) {
  PointerEvent e;
  e.ray = Ray{Vec3{x, y, -5.0}, Vec3{0.0, 0.0, 1.0}};
  e.tols = PickOptions{vertexTol, edgeTol};
  return e;
}

TEST(BuildContextMenuTest, EdgeHitReturnsDivideAndErase) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{PickKind::Edge, 7, Vec3{}, 0.0};

  int eraseCount = 0;
  kernel.dispatcher().subscribe<RemoveEdgeRequested>(
      &eraseCount, [&](const RemoveEdgeRequested &ev) {
        ++eraseCount;
        EXPECT_EQ(ev.edgeId, 7);
      });

  std::optional<Id> startedDivideEdge;
  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0),
      [&](Id id) { startedDivideEdge = id; }, [](Id) {});

  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].label, "Divide");
  EXPECT_EQ(items[1].label, "Erase");
  EXPECT_FALSE(items[0].checkable);

  items[0].action();
  ASSERT_TRUE(startedDivideEdge.has_value());
  EXPECT_EQ(*startedDivideEdge, 7);

  items[1].action();
  EXPECT_EQ(eraseCount, 1);
}

TEST(BuildContextMenuTest, GuidePointHitReturnsEraseHideAndDeleteAllGuides) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{}; // miss -- falls through to guide hit-testing
  ctx.guidePointsData = {GuidePointData{Vec3{0, 0, 0}, 42}};

  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {}, [](Id) {});

  ASSERT_EQ(items.size(), 3u);
  EXPECT_EQ(items[0].label, "Erase");
  EXPECT_EQ(items[1].label, "Hide");
  EXPECT_EQ(items[2].label, "Delete All Guides");
}

TEST(BuildContextMenuTest, GuideLineHitIsIgnoredWhenACloserGuidePointExists) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{};
  ctx.guidePointsData = {GuidePointData{Vec3{0, 0, 0}, 1}};
  ctx.guideLinesData = {GuideLineData{Vec3{0, 0, 0}, Vec3{1, 0, 0}, 2}};

  int erasedId = kInvalidId;
  kernel.dispatcher().subscribe<EraseGuideRequested>(
      &erasedId, [&](const EraseGuideRequested &ev) { erasedId = ev.id; });

  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {}, [](Id) {});
  ASSERT_FALSE(items.empty());
  items[0].action(); // "Erase"

  EXPECT_EQ(erasedId, 1); // the guide point (id 1) beats the line (id 2) on a tie
}

TEST(BuildContextMenuTest, SectionPlaneHitReturnsReverseAndCheckableActiveCut) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{};
  SectionPlaneData plane;
  plane.id = 9;
  plane.point = Vec3{0, 0, 0};
  plane.normal = Vec3{0, 0, 1};
  plane.active = false;
  plane.hidden = false;
  ctx.sectionsData = {plane};

  int activeRequests = 0;
  kernel.dispatcher().subscribe<SetSectionActiveRequested>(
      &activeRequests, [&](const SetSectionActiveRequested &ev) {
        ++activeRequests;
        EXPECT_EQ(ev.id, 9);
        EXPECT_TRUE(ev.active); // toggled from false
      });

  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {}, [](Id) {});

  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].label, "Reverse");
  EXPECT_EQ(items[1].label, "Active Cut");
  EXPECT_TRUE(items[1].checkable);
  EXPECT_FALSE(items[1].checked); // mirrors plane.active

  items[1].action();
  EXPECT_EQ(activeRequests, 1);
}

TEST(BuildContextMenuTest, HiddenSectionPlaneIsNotHitTestable) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{};
  SectionPlaneData plane;
  plane.id = 9;
  plane.point = Vec3{0, 0, 0};
  plane.normal = Vec3{0, 0, 1};
  plane.hidden = true;
  ctx.sectionsData = {plane};
  // Off the default axes origin so the axes hit-test can't mask the hidden-section skip.
  ctx.axesFrameData.origin = Vec3{100, 100, 100};

  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {}, [](Id) {});

  EXPECT_TRUE(items.empty());
}

TEST(BuildContextMenuTest, AxesOriginHitReturnsReset) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{};
  ctx.axesFrameData = AxesFrame{}; // default: origin at world zero

  int resetCount = 0;
  kernel.dispatcher().subscribe<ResetAxesRequested>(
      &resetCount, [&](const ResetAxesRequested &) { ++resetCount; });

  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {}, [](Id) {});

  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].label, "Reset");
  items[0].action();
  EXPECT_EQ(resetCount, 1);
}

TEST(BuildContextMenuTest, NothingUnderCursorReturnsEmpty) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{};

  // Far from the axes origin, no guides, no sections -- nothing to hit.
  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(50, 50), [](Id) {}, [](Id) {});

  EXPECT_TRUE(items.empty());
}

// -- Face-hit branch --------

TEST(BuildContextMenuTest, TexturedFaceHitReturnsPositionTexture) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{PickKind::Face, 55, Vec3{}, 0.0};
  ctx.frontMaterialOfData = 3;
  ctx.materialTextureInfoData =
      ToolContext::MaterialTextureInfo{true, 2.0, 3.0};
  // Away from the axes origin so the axes hit-test doesn't fire first.
  ctx.axesFrameData.origin = Vec3{100, 100, 100};

  std::optional<Id> startedPositionTextureFace;
  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {},
      [&](Id id) { startedPositionTextureFace = id; });

  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].label, "Position Texture");
  EXPECT_FALSE(items[0].checkable);

  items[0].action();
  ASSERT_TRUE(startedPositionTextureFace.has_value());
  EXPECT_EQ(*startedPositionTextureFace, 55);
}

TEST(BuildContextMenuTest, UntexturedFaceHitReturnsEmpty) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{PickKind::Face, 55, Vec3{}, 0.0};
  ctx.frontMaterialOfData = 3;
  ctx.materialTextureInfoData =
      ToolContext::MaterialTextureInfo{false, 1.0, 1.0};
  ctx.axesFrameData.origin = Vec3{100, 100, 100};

  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {}, [](Id) {});

  EXPECT_TRUE(items.empty());
}

TEST(BuildContextMenuTest, UnpaintedFaceHitReturnsEmpty) {
  Kernel kernel;
  FakeToolContext ctx;
  ctx.pickResult = PickResult{PickKind::Face, 55, Vec3{}, 0.0};
  // frontMaterialOfData 0 = unpainted; reads as untextured.
  ctx.axesFrameData.origin = Vec3{100, 100, 100};

  const std::vector<ContextMenuItem> items = buildContextMenu(
      ctx, kernel.presenterContext(), rayAt(0, 0), [](Id) {}, [](Id) {});

  EXPECT_TRUE(items.empty());
}

} // namespace
