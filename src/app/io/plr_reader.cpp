#include "io/plr_reader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/vec3.h>

#include "agent/asset_repository.h"
#include "agent/events.h"
#include "io/plr_container.h"
#include "io/plr_format.h"

// Mirror of plr_writer.cpp: consumes exactly the schema it documents.
// (a) sniffContainer() gate + QJsonDocument::fromJson parse.
// (b) validates into a local ParsedDocument + staging geo::Scene -- no agent
// argument is touched, so a failure leaves everything untouched.
// (c) apply -- clearForRestore() on all seven agents, adopt the staging
// scene, replay restore* calls, refreshAssociations().

namespace plnr::io {

namespace {

using agent::Dimension;
using agent::Frame;
using agent::Guide;
using agent::Material;
using agent::SectionPlane;
using agent::Tag;
using agent::TextNote;
using events::EntityRef;

QString err(const QString& loc, const QString& msg) {
    return loc + QStringLiteral(": ") + msg;
}

QString idx(const QString& base, qsizetype i) {
    return base + QStringLiteral("[") + QString::number(i) + QStringLiteral("]");
}

bool asDouble(const QJsonValue& v, double& out) {
    if (!v.isDouble()) {
        return false;
    }
    out = v.toDouble();
    return true;
}

// Well-formed integer (any sign) -- used for shadows.month/shadows.day. No
// range validation (1-12/1-31 is ShadowStore's own concern -- this reader
// validates JSON structure, not domain range).
bool asInt(const QJsonValue& v, int& out) {
    double d = 0.0;
    if (!asDouble(v, d) || d != std::floor(d)) {
        return false;
    }
    out = static_cast<int>(d);
    return true;
}

// Positive (> 0) well-formed integer, used for every id in the schema
// (vertex/edge/face/definition/instance/tag/guide/dimension/text/section).
bool asId(const QJsonValue& v, geo::Id& out) {
    double d = 0.0;
    if (!asDouble(v, d) || d < 1.0 || d != std::floor(d)) {
        return false;
    }
    out = static_cast<geo::Id>(d);
    return true;
}

bool asUint64(const QJsonValue& v, std::uint64_t& out) {
    geo::Id id = 0;
    if (!asId(v, id)) {
        return false;
    }
    out = id;
    return true;
}

// Non-negative (>= 0) well-formed integer -- used for materialAssignments'
// front/back slots, where (unlike every other id in the schema) 0 is a
// legal, meaningful value ("no material assigned to this side").
bool asMaterialSlotId(const QJsonValue& v, geo::Id& out) {
    double d = 0.0;
    if (!asDouble(v, d) || d < 0.0 || d != std::floor(d)) {
        return false;
    }
    out = static_cast<geo::Id>(d);
    return true;
}

// A materialAssignments row's optional 5th element -- {du,dv,rot,su,sv}, every
// field a required number. Only called when the row has one.
bool parseUvTransform(const QJsonValue& val, const QString& loc, events::UvTransform& out, QString& error) {
    if (!val.isObject()) {
        error = err(loc, QStringLiteral("5th element must be an object {du,dv,rot,su,sv}"));
        return false;
    }
    const QJsonObject obj = val.toObject();
    if (!asDouble(obj.value("du"), out.offsetU)) {
        error = err(loc, QStringLiteral("du must be a number"));
        return false;
    }
    if (!asDouble(obj.value("dv"), out.offsetV)) {
        error = err(loc, QStringLiteral("dv must be a number"));
        return false;
    }
    if (!asDouble(obj.value("rot"), out.rotationRad)) {
        error = err(loc, QStringLiteral("rot must be a number"));
        return false;
    }
    if (!asDouble(obj.value("su"), out.scaleU)) {
        error = err(loc, QStringLiteral("su must be a number"));
        return false;
    }
    if (!asDouble(obj.value("sv"), out.scaleV)) {
        error = err(loc, QStringLiteral("sv must be a number"));
        return false;
    }
    return true;
}

bool asBool(const QJsonValue& v, bool& out) {
    if (!v.isBool()) {
        return false;
    }
    out = v.toBool();
    return true;
}

bool asString(const QJsonValue& v, QString& out) {
    if (!v.isString()) {
        return false;
    }
    out = v.toString();
    return true;
}

bool asVec3(const QJsonValue& v, geo::Vec3& out) {
    if (!v.isArray()) {
        return false;
    }
    const QJsonArray arr = v.toArray();
    if (arr.size() != 3) {
        return false;
    }
    double x = 0.0, y = 0.0, z = 0.0;
    if (!asDouble(arr[0], x) || !asDouble(arr[1], y) || !asDouble(arr[2], z)) {
        return false;
    }
    out = geo::Vec3{x, y, z};
    return true;
}

// Inverse of plr_writer.cpp's kindToString.
bool asKind(const QString& s, geo::EntityKind& out) {
    if (s == QStringLiteral("vertex")) {
        out = geo::EntityKind::Vertex;
        return true;
    }
    if (s == QStringLiteral("edge")) {
        out = geo::EntityKind::Edge;
        return true;
    }
    if (s == QStringLiteral("face")) {
        out = geo::EntityKind::Face;
        return true;
    }
    if (s == QStringLiteral("instance")) {
        out = geo::EntityKind::Instance;
        return true;
    }
    return false;
}

// -- Parsed mesh records (geometry-only; not the domain structs) ----------

struct ParsedVertex {
    geo::Id id{};
    geo::Vec3 pos;
};

struct ParsedEdge {
    geo::Id id{};
    geo::Id v0{};
    geo::Id v1{};
};

struct ParsedFace {
    geo::Id id{};
    std::vector<geo::Id> loop;
};

struct ParsedMesh {
    std::vector<ParsedVertex> vertices;
    std::vector<ParsedEdge> edges;
    std::vector<ParsedFace> faces;
};

struct ParsedInstance {
    geo::Id id{};
    geo::Id definitionId{};
    std::string name;
    geo::Transform transform;
};

struct ParsedDefinition {
    geo::Id id{};
    std::string name;
    bool isGroup{};
    ParsedMesh mesh;
    std::vector<ParsedInstance> instances;
};

struct ParsedAssignment {
    EntityRef ref;
    std::uint64_t tagId{};
};

struct ParsedMaterialAssignment {
    EntityRef ref;
    geo::Id front{};
    geo::Id back{};
    // Identity for the 4-element row form; parsed for the optional 5th element.
    events::UvTransform uvTransform;
};

// Parsed contents of the OPTIONAL top-level `style` object. present is true
// only when the file carried the key, distinguishing "absent" from "present
// but all-default".
struct ParsedStyle {
    bool present{};
    events::FaceStyle faceStyle{events::FaceStyle::ShadedWithTextures};
    bool profiles{};
    bool depthCue{};
    bool backEdges{};
    bool ambientOcclusion{};
    double aoStrength{agent::kDefaultAoStrength};
    agent::StyleColor frontColor;
    agent::StyleColor backColor;
};

// Parsed contents of the OPTIONAL top-level `shadows` object -- same
// present/absent discipline as ParsedStyle. `enabled` renamed to
// `useSunForShading`; `showShadows`/`light`/`dark` are new fields.
struct ParsedShadows {
    bool present{};
    bool useSunForShading{};
    bool showShadows{};
    double latitudeDeg{agent::kDefaultLatitudeDeg};
    double longitudeDeg{agent::kDefaultLongitudeDeg};
    int month{agent::kDefaultMonth};
    int day{agent::kDefaultDay};
    double hourLocal{agent::kDefaultHourLocal};
    double light{agent::kDefaultLight};
    double dark{agent::kDefaultDark};
};

// Parsed contents of the OPTIONAL top-level `fog` object -- same
// present/absent discipline as ParsedShadows above.
struct ParsedFog {
    bool present{};
    bool enabled{};
    double startDistance{agent::kDefaultFogStartDistance};
    double endDistance{agent::kDefaultFogEndDistance};
    bool useBackgroundColor{true};
    agent::StyleColor color{0.5, 0.5, 0.5};
};

// Everything parsed out of a .plr JSON document -- filled by the parseXxx()
// helpers below, entirely local (no agent touched yet).
struct ParsedDocument {
    std::vector<ParsedDefinition> definitions;
    std::vector<Tag> tags;
    std::vector<ParsedAssignment> assignments;
    std::vector<EntityRef> hidden;
    std::vector<Guide> guides;
    std::vector<Dimension> dimensions;
    std::vector<TextNote> texts;
    std::vector<SectionPlane> sectionPlanes;
    Frame axesFrame;
    std::vector<Material> materials;
    std::vector<ParsedMaterialAssignment> materialAssignments;
    ParsedStyle style;
    ParsedShadows shadows;
    ParsedFog fog;
    CameraState camera;
    DocumentMeta meta;
};

// One definition's mesh: vertices, then edges (endpoints resolve within this
// mesh), then faces (loop entries resolve within this mesh, each consecutive
// pair, wrap included, connected by an edge already parsed). vertices/edges/
// faces share one id space per mesh.
bool parseMesh(const QJsonValue& meshVal, const QString& defLoc, ParsedMesh& mesh, QString& error) {
    if (!meshVal.isObject()) {
        error = err(defLoc, QStringLiteral("mesh must be an object"));
        return false;
    }
    const QJsonObject meshObj = meshVal.toObject();

    if (!meshObj.value("vertices").isArray()) {
        error = err(defLoc, QStringLiteral("mesh.vertices must be an array"));
        return false;
    }
    if (!meshObj.value("edges").isArray()) {
        error = err(defLoc, QStringLiteral("mesh.edges must be an array"));
        return false;
    }
    if (!meshObj.value("faces").isArray()) {
        error = err(defLoc, QStringLiteral("mesh.faces must be an array"));
        return false;
    }

    std::unordered_set<geo::Id> usedMeshIds;
    std::unordered_set<geo::Id> vertexIds;
    std::set<std::pair<geo::Id, geo::Id>> edgeLookup;  // normalized (min, max) pairs

    const QJsonArray verticesArr = meshObj.value("vertices").toArray();
    mesh.vertices.reserve(static_cast<std::size_t>(verticesArr.size()));
    for (qsizetype i = 0; i < verticesArr.size(); ++i) {
        const QString loc = idx(defLoc + QStringLiteral(".mesh.vertices"), i);
        if (!verticesArr[i].isArray()) {
            error = err(loc, QStringLiteral("must be an array"));
            return false;
        }
        const QJsonArray v = verticesArr[i].toArray();
        if (v.size() != 4) {
            error = err(loc, QStringLiteral("must be [id, x, y, z]"));
            return false;
        }
        geo::Id id = 0;
        if (!asId(v[0], id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (usedMeshIds.count(id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(id) + QStringLiteral(" within mesh"));
            return false;
        }
        double x = 0.0, y = 0.0, z = 0.0;
        if (!asDouble(v[1], x) || !asDouble(v[2], y) || !asDouble(v[3], z)) {
            error = err(loc, QStringLiteral("position must be 3 numbers"));
            return false;
        }
        usedMeshIds.insert(id);
        vertexIds.insert(id);
        mesh.vertices.push_back(ParsedVertex{id, geo::Vec3{x, y, z}});
    }

    const QJsonArray edgesArr = meshObj.value("edges").toArray();
    mesh.edges.reserve(static_cast<std::size_t>(edgesArr.size()));
    for (qsizetype i = 0; i < edgesArr.size(); ++i) {
        const QString loc = idx(defLoc + QStringLiteral(".mesh.edges"), i);
        if (!edgesArr[i].isArray()) {
            error = err(loc, QStringLiteral("must be an array"));
            return false;
        }
        const QJsonArray e = edgesArr[i].toArray();
        if (e.size() != 3) {
            error = err(loc, QStringLiteral("must be [id, v0, v1]"));
            return false;
        }
        geo::Id id = 0, v0 = 0, v1 = 0;
        if (!asId(e[0], id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (usedMeshIds.count(id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(id) + QStringLiteral(" within mesh"));
            return false;
        }
        if (!asId(e[1], v0) || !asId(e[2], v1)) {
            error = err(loc, QStringLiteral("v0/v1 must be positive integers"));
            return false;
        }
        if (v0 == v1) {
            error = err(loc, QStringLiteral("edge endpoints must differ"));
            return false;
        }
        if (vertexIds.count(v0) == 0) {
            error = err(loc, QStringLiteral("edge references unknown vertex ") + QString::number(v0));
            return false;
        }
        if (vertexIds.count(v1) == 0) {
            error = err(loc, QStringLiteral("edge references unknown vertex ") + QString::number(v1));
            return false;
        }
        usedMeshIds.insert(id);
        edgeLookup.insert({std::min(v0, v1), std::max(v0, v1)});
        mesh.edges.push_back(ParsedEdge{id, v0, v1});
    }

    const QJsonArray facesArr = meshObj.value("faces").toArray();
    mesh.faces.reserve(static_cast<std::size_t>(facesArr.size()));
    for (qsizetype i = 0; i < facesArr.size(); ++i) {
        const QString loc = idx(defLoc + QStringLiteral(".mesh.faces"), i);
        if (!facesArr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject f = facesArr[i].toObject();
        geo::Id id = 0;
        if (!asId(f.value("id"), id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (usedMeshIds.count(id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(id) + QStringLiteral(" within mesh"));
            return false;
        }
        if (!f.value("loop").isArray()) {
            error = err(loc, QStringLiteral("loop must be an array"));
            return false;
        }
        const QJsonArray loopArr = f.value("loop").toArray();
        if (loopArr.size() < 3) {
            error = err(loc, QStringLiteral("loop must have at least 3 vertices"));
            return false;
        }
        std::vector<geo::Id> loop;
        loop.reserve(static_cast<std::size_t>(loopArr.size()));
        for (qsizetype k = 0; k < loopArr.size(); ++k) {
            geo::Id vId = 0;
            if (!asId(loopArr[k], vId)) {
                error = err(loc, QStringLiteral("loop entries must be positive integers"));
                return false;
            }
            if (vertexIds.count(vId) == 0) {
                error = err(loc, QStringLiteral("loop references unknown vertex ") + QString::number(vId));
                return false;
            }
            loop.push_back(vId);
        }
        for (std::size_t k = 0; k < loop.size(); ++k) {
            const geo::Id a = loop[k];
            const geo::Id b = loop[(k + 1) % loop.size()];
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            if (edgeLookup.count(key) == 0) {
                error = err(loc, QStringLiteral("loop has no edge between vertices ") + QString::number(a) +
                                      QStringLiteral(" and ") + QString::number(b));
                return false;
            }
        }
        usedMeshIds.insert(id);
        mesh.faces.push_back(ParsedFace{id, std::move(loop)});
    }
    return true;
}

// One "definitions" entry's top-level fields, held for the second pass --
// an instance's definitionId may name a definition appearing later in the array.
struct RawDefEntry {
    geo::Id id{};
    std::string name;
    bool isGroup{};
    QJsonValue meshVal;
    QJsonArray instancesArr;
};

bool collectDefinitionIds(const QJsonArray& defsArr, std::vector<RawDefEntry>& rawDefs,
                           std::unordered_set<geo::Id>& definitionIds, QString& error) {
    rawDefs.reserve(static_cast<std::size_t>(defsArr.size()));
    for (qsizetype i = 0; i < defsArr.size(); ++i) {
        const QString loc = idx(QStringLiteral("definitions"), i);
        if (!defsArr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject obj = defsArr[i].toObject();
        RawDefEntry entry;
        if (!asId(obj.value("id"), entry.id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (definitionIds.count(entry.id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(entry.id));
            return false;
        }
        QString name;
        if (!asString(obj.value("name"), name)) {
            error = err(loc, QStringLiteral("name must be a string"));
            return false;
        }
        entry.name = name.toStdString();
        if (!asBool(obj.value("isGroup"), entry.isGroup)) {
            error = err(loc, QStringLiteral("isGroup must be a bool"));
            return false;
        }
        entry.meshVal = obj.value("mesh");
        if (!obj.value("instances").isArray()) {
            error = err(loc, QStringLiteral("instances must be an array"));
            return false;
        }
        entry.instancesArr = obj.value("instances").toArray();
        definitionIds.insert(entry.id);
        rawDefs.push_back(std::move(entry));
    }
    if (definitionIds.count(geo::kRootDefinitionId) == 0) {
        error = QStringLiteral("definitions: missing root definition (id 1)");
        return false;
    }
    return true;
}

// The scene-wide id space (definitions + instances share one counter, per
// geo::Scene) is seeded with every definition id, then grown as each
// instance is validated, so a duplicate is caught regardless of which two
// records collide.
bool parseDefinitions(const QJsonArray& defsArr, std::vector<ParsedDefinition>& outDefs, QString& error) {
    std::vector<RawDefEntry> rawDefs;
    std::unordered_set<geo::Id> definitionIds;
    if (!collectDefinitionIds(defsArr, rawDefs, definitionIds, error)) {
        return false;
    }

    std::unordered_set<geo::Id> sceneIds = definitionIds;

    outDefs.reserve(rawDefs.size());
    for (std::size_t i = 0; i < rawDefs.size(); ++i) {
        const RawDefEntry& raw = rawDefs[i];
        const QString defLoc = idx(QStringLiteral("definitions"), static_cast<qsizetype>(i));

        ParsedDefinition def;
        def.id = raw.id;
        def.name = raw.name;
        def.isGroup = raw.isGroup;
        if (!parseMesh(raw.meshVal, defLoc, def.mesh, error)) {
            return false;
        }

        def.instances.reserve(static_cast<std::size_t>(raw.instancesArr.size()));
        for (qsizetype j = 0; j < raw.instancesArr.size(); ++j) {
            const QString loc = idx(defLoc + QStringLiteral(".instances"), j);
            if (!raw.instancesArr[j].isObject()) {
                error = err(loc, QStringLiteral("must be an object"));
                return false;
            }
            const QJsonObject instObj = raw.instancesArr[j].toObject();
            geo::Id instId = 0, defId = 0;
            if (!asId(instObj.value("id"), instId)) {
                error = err(loc, QStringLiteral("id must be a positive integer"));
                return false;
            }
            if (sceneIds.count(instId) != 0) {
                error = err(loc, QStringLiteral("duplicate id ") + QString::number(instId));
                return false;
            }
            if (!asId(instObj.value("definitionId"), defId)) {
                error = err(loc, QStringLiteral("definitionId must be a positive integer"));
                return false;
            }
            if (definitionIds.count(defId) == 0) {
                error = err(loc, QStringLiteral("unknown definitionId ") + QString::number(defId));
                return false;
            }
            if (defId == raw.id) {
                error = err(loc, QStringLiteral("instance may not reference its own parent definition"));
                return false;
            }
            QString name;
            if (!asString(instObj.value("name"), name)) {
                error = err(loc, QStringLiteral("name must be a string"));
                return false;
            }
            if (!instObj.value("transform").isArray()) {
                error = err(loc, QStringLiteral("transform must be an array of 12 numbers"));
                return false;
            }
            const QJsonArray xf = instObj.value("transform").toArray();
            if (xf.size() != 12) {
                error = err(loc, QStringLiteral("transform must be an array of 12 numbers"));
                return false;
            }
            double n[12];
            for (int k = 0; k < 12; ++k) {
                if (!asDouble(xf[k], n[k])) {
                    error = err(loc, QStringLiteral("transform entries must be numbers"));
                    return false;
                }
            }
            geo::Transform transform;
            transform.col0 = geo::Vec3{n[0], n[1], n[2]};
            transform.col1 = geo::Vec3{n[3], n[4], n[5]};
            transform.col2 = geo::Vec3{n[6], n[7], n[8]};
            transform.t = geo::Vec3{n[9], n[10], n[11]};

            sceneIds.insert(instId);
            def.instances.push_back(ParsedInstance{instId, defId, name.toStdString(), transform});
        }
        outDefs.push_back(std::move(def));
    }
    return true;
}

// Tag id 1 must be present and named "Untagged" -- the agent's own ctor
// seed, always replayed explicitly by the writer.
bool parseTags(const QJsonValue& val, std::vector<Tag>& outTags, QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("tags must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    std::unordered_set<std::uint64_t> ids;
    bool sawUntagged = false;
    outTags.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("tags"), i);
        if (!arr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject obj = arr[i].toObject();
        Tag tag;
        if (!asUint64(obj.value("id"), tag.id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (ids.count(tag.id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(tag.id));
            return false;
        }
        QString name;
        if (!asString(obj.value("name"), name)) {
            error = err(loc, QStringLiteral("name must be a string"));
            return false;
        }
        tag.name = name.toStdString();
        if (!asBool(obj.value("visible"), tag.visible)) {
            error = err(loc, QStringLiteral("visible must be a bool"));
            return false;
        }
        if (tag.id == agent::kUntaggedTagId) {
            if (tag.name != "Untagged") {
                error = err(loc, QStringLiteral("tag id 1 must be named \"Untagged\""));
                return false;
            }
            sawUntagged = true;
        }
        ids.insert(tag.id);
        outTags.push_back(std::move(tag));
    }
    if (!sawUntagged) {
        error = QStringLiteral("tags: missing tag id 1 (Untagged)");
        return false;
    }
    return true;
}

// tagId must name a listed tag (tagIds, from parseTags above); the ref
// itself is accepted verbatim -- not checked against the mesh (a stale
// assignment is legal agent state).
bool parseTagAssignments(const QJsonValue& val, const std::unordered_set<std::uint64_t>& tagIds,
                          std::vector<ParsedAssignment>& out, QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("tagAssignments must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("tagAssignments"), i);
        if (!arr[i].isArray()) {
            error = err(loc, QStringLiteral("must be an array"));
            return false;
        }
        const QJsonArray e = arr[i].toArray();
        if (e.size() != 3) {
            error = err(loc, QStringLiteral("must be [kind, id, tagId]"));
            return false;
        }
        QString kindStr;
        if (!asString(e[0], kindStr)) {
            error = err(loc, QStringLiteral("kind must be a string"));
            return false;
        }
        geo::EntityKind kind{};
        if (!asKind(kindStr, kind)) {
            error = err(loc, QStringLiteral("invalid kind \"") + kindStr + QStringLiteral("\""));
            return false;
        }
        geo::Id id = 0;
        if (!asId(e[1], id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        std::uint64_t tagId = 0;
        if (!asUint64(e[2], tagId)) {
            error = err(loc, QStringLiteral("tagId must be a positive integer"));
            return false;
        }
        if (tagIds.count(tagId) == 0) {
            error = err(loc, QStringLiteral("unknown tagId ") + QString::number(tagId));
            return false;
        }
        out.push_back(ParsedAssignment{EntityRef{kind, id}, tagId});
    }
    return true;
}

// Same "accepted verbatim, not checked against the mesh" contract as
// tagAssignments above.
bool parseHidden(const QJsonValue& val, std::vector<EntityRef>& out, QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("hidden must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("hidden"), i);
        if (!arr[i].isArray()) {
            error = err(loc, QStringLiteral("must be an array"));
            return false;
        }
        const QJsonArray e = arr[i].toArray();
        if (e.size() != 2) {
            error = err(loc, QStringLiteral("must be [kind, id]"));
            return false;
        }
        QString kindStr;
        if (!asString(e[0], kindStr)) {
            error = err(loc, QStringLiteral("kind must be a string"));
            return false;
        }
        geo::EntityKind kind{};
        if (!asKind(kindStr, kind)) {
            error = err(loc, QStringLiteral("invalid kind \"") + kindStr + QStringLiteral("\""));
            return false;
        }
        geo::Id id = 0;
        if (!asId(e[1], id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        out.push_back(EntityRef{kind, id});
    }
    return true;
}

bool parseGuides(const QJsonValue& val, std::vector<Guide>& out, QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("guides must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    std::unordered_set<geo::Id> ids;
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("guides"), i);
        if (!arr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject obj = arr[i].toObject();
        Guide g;
        if (!asId(obj.value("id"), g.id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (ids.count(g.id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(g.id));
            return false;
        }
        if (!asBool(obj.value("isLine"), g.isLine)) {
            error = err(loc, QStringLiteral("isLine must be a bool"));
            return false;
        }
        if (!asVec3(obj.value("point"), g.point)) {
            error = err(loc, QStringLiteral("point must be 3 numbers"));
            return false;
        }
        if (!asVec3(obj.value("dir"), g.dir)) {
            error = err(loc, QStringLiteral("dir must be 3 numbers"));
            return false;
        }
        if (!asBool(obj.value("hidden"), g.hidden)) {
            error = err(loc, QStringLiteral("hidden must be a bool"));
            return false;
        }
        ids.insert(g.id);
        out.push_back(std::move(g));
    }
    return true;
}

// dimensions/texts share one id counter -- annotationIds is threaded
// through both parseDimensions and parseTexts so a cross-collection clash
// is caught, not just within one of them.
bool parseDimensions(const QJsonValue& val, std::unordered_set<geo::Id>& annotationIds, std::vector<Dimension>& out,
                      QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("dimensions must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("dimensions"), i);
        if (!arr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject obj = arr[i].toObject();
        Dimension d;
        if (!asId(obj.value("id"), d.id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (annotationIds.count(d.id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(d.id));
            return false;
        }
        // vertexA/vertexB are accepted verbatim -- not checked against the
        // mesh (a stale vertex ref is legal agent state).
        if (!asId(obj.value("vertexA"), d.vertexA)) {
            error = err(loc, QStringLiteral("vertexA must be a positive integer"));
            return false;
        }
        if (!asId(obj.value("vertexB"), d.vertexB)) {
            error = err(loc, QStringLiteral("vertexB must be a positive integer"));
            return false;
        }
        if (!asVec3(obj.value("offsetDir"), d.offsetDir)) {
            error = err(loc, QStringLiteral("offsetDir must be 3 numbers"));
            return false;
        }
        if (!asDouble(obj.value("offset"), d.offset)) {
            error = err(loc, QStringLiteral("offset must be a number"));
            return false;
        }
        QString overrideText;
        if (!asString(obj.value("overrideText"), overrideText)) {
            error = err(loc, QStringLiteral("overrideText must be a string"));
            return false;
        }
        d.overrideText = overrideText.toStdString();
        if (!asBool(obj.value("associated"), d.associated)) {
            error = err(loc, QStringLiteral("associated must be a bool"));
            return false;
        }
        if (!asVec3(obj.value("lastA"), d.lastA)) {
            error = err(loc, QStringLiteral("lastA must be 3 numbers"));
            return false;
        }
        if (!asVec3(obj.value("lastB"), d.lastB)) {
            error = err(loc, QStringLiteral("lastB must be 3 numbers"));
            return false;
        }
        annotationIds.insert(d.id);
        out.push_back(std::move(d));
    }
    return true;
}

bool parseTexts(const QJsonValue& val, std::unordered_set<geo::Id>& annotationIds, std::vector<TextNote>& out,
                 QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("texts must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("texts"), i);
        if (!arr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject obj = arr[i].toObject();
        TextNote t;
        if (!asId(obj.value("id"), t.id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (annotationIds.count(t.id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(t.id));
            return false;
        }
        if (!asBool(obj.value("screenFixed"), t.screenFixed)) {
            error = err(loc, QStringLiteral("screenFixed must be a bool"));
            return false;
        }
        if (!asDouble(obj.value("screenX"), t.screenX)) {
            error = err(loc, QStringLiteral("screenX must be a number"));
            return false;
        }
        if (!asDouble(obj.value("screenY"), t.screenY)) {
            error = err(loc, QStringLiteral("screenY must be a number"));
            return false;
        }
        if (!asVec3(obj.value("worldAnchor"), t.worldAnchor)) {
            error = err(loc, QStringLiteral("worldAnchor must be 3 numbers"));
            return false;
        }
        const QJsonValue leaderVal = obj.value("leaderTarget");
        if (leaderVal.isNull()) {
            t.leaderTarget.reset();
        } else if (leaderVal.isArray()) {
            const QJsonArray lt = leaderVal.toArray();
            if (lt.size() != 2) {
                error = err(loc, QStringLiteral("leaderTarget must be [kind, id] or null"));
                return false;
            }
            QString kindStr;
            if (!asString(lt[0], kindStr)) {
                error = err(loc, QStringLiteral("leaderTarget kind must be a string"));
                return false;
            }
            geo::EntityKind kind{};
            if (!asKind(kindStr, kind)) {
                error = err(loc, QStringLiteral("leaderTarget: invalid kind \"") + kindStr + QStringLiteral("\""));
                return false;
            }
            geo::Id targetId = 0;
            // leaderTarget's id is accepted verbatim -- NOT checked against
            // the mesh, same as vertexA/vertexB/hidden/tagAssignments.
            if (!asId(lt[1], targetId)) {
                error = err(loc, QStringLiteral("leaderTarget id must be a positive integer"));
                return false;
            }
            t.leaderTarget = EntityRef{kind, targetId};
        } else {
            error = err(loc, QStringLiteral("leaderTarget must be [kind, id] or null"));
            return false;
        }
        QString text;
        if (!asString(obj.value("text"), text)) {
            error = err(loc, QStringLiteral("text must be a string"));
            return false;
        }
        t.text = text.toStdString();
        annotationIds.insert(t.id);
        out.push_back(std::move(t));
    }
    return true;
}

bool parseSectionPlanes(const QJsonValue& val, std::vector<SectionPlane>& out, QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("sectionPlanes must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    std::unordered_set<geo::Id> ids;
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("sectionPlanes"), i);
        if (!arr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject obj = arr[i].toObject();
        SectionPlane p;
        if (!asId(obj.value("id"), p.id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (ids.count(p.id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(p.id));
            return false;
        }
        QString name;
        if (!asString(obj.value("name"), name)) {
            error = err(loc, QStringLiteral("name must be a string"));
            return false;
        }
        p.name = name.toStdString();
        if (!asVec3(obj.value("point"), p.point)) {
            error = err(loc, QStringLiteral("point must be 3 numbers"));
            return false;
        }
        if (!asVec3(obj.value("normal"), p.normal)) {
            error = err(loc, QStringLiteral("normal must be 3 numbers"));
            return false;
        }
        if (!asBool(obj.value("active"), p.active)) {
            error = err(loc, QStringLiteral("active must be a bool"));
            return false;
        }
        if (!asBool(obj.value("hidden"), p.hidden)) {
            error = err(loc, QStringLiteral("hidden must be a bool"));
            return false;
        }
        ids.insert(p.id);
        out.push_back(std::move(p));
    }
    return true;
}

bool parseAxes(const QJsonValue& val, Frame& out, QString& error) {
    if (!val.isObject()) {
        error = QStringLiteral("axes must be an object");
        return false;
    }
    const QJsonObject obj = val.toObject();
    if (!asVec3(obj.value("origin"), out.origin)) {
        error = QStringLiteral("axes.origin must be 3 numbers");
        return false;
    }
    if (!asVec3(obj.value("x"), out.xDir)) {
        error = QStringLiteral("axes.x must be 3 numbers");
        return false;
    }
    if (!asVec3(obj.value("y"), out.yDir)) {
        error = QStringLiteral("axes.y must be 3 numbers");
        return false;
    }
    if (!asVec3(obj.value("z"), out.zDir)) {
        error = QStringLiteral("axes.z must be 3 numbers");
        return false;
    }
    return true;
}

// texture: null -> m stays untextured; an object -> {assetHash (non-empty),
// tileW, tileH} all required. Does not resolve assetHash against any asset
// agent -- that cross-check is readDocument's own job.
bool parseMaterialTexture(const QJsonValue& val, const QString& loc, Material& m, QString& error) {
    if (val.isNull() || val.isUndefined()) {
        return true;  // untextured (or key absent -- tolerated the same way) -- m's fields already default-initialized
    }
    if (!val.isObject()) {
        error = err(loc, QStringLiteral("texture must be an object or null"));
        return false;
    }
    const QJsonObject obj = val.toObject();
    QString assetHash;
    if (!asString(obj.value("assetHash"), assetHash) || assetHash.isEmpty()) {
        error = err(loc, QStringLiteral("texture.assetHash must be a non-empty string"));
        return false;
    }
    if (!asDouble(obj.value("tileW"), m.tileW)) {
        error = err(loc, QStringLiteral("texture.tileW must be a number"));
        return false;
    }
    if (!asDouble(obj.value("tileH"), m.tileH)) {
        error = err(loc, QStringLiteral("texture.tileH must be a number"));
        return false;
    }
    m.assetHash = assetHash.toStdString();
    return true;
}

// Accepts the legacy `null` sentinel (outMaterials stays empty) or the array
// form. pbr is a reserved field: accepted if present, not validated or stored.
bool parseMaterials(const QJsonValue& val, std::vector<Material>& outMaterials, QString& error) {
    if (val.isNull()) {
        return true;  // no materials -- outMaterials stays empty
    }
    if (!val.isArray()) {
        error = QStringLiteral("materials must be an array or null");
        return false;
    }
    const QJsonArray arr = val.toArray();
    std::unordered_set<geo::Id> ids;
    outMaterials.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("materials"), i);
        if (!arr[i].isObject()) {
            error = err(loc, QStringLiteral("must be an object"));
            return false;
        }
        const QJsonObject obj = arr[i].toObject();
        Material m;
        if (!asId(obj.value("id"), m.id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        if (ids.count(m.id) != 0) {
            error = err(loc, QStringLiteral("duplicate id ") + QString::number(m.id));
            return false;
        }
        QString name;
        if (!asString(obj.value("name"), name)) {
            error = err(loc, QStringLiteral("name must be a string"));
            return false;
        }
        m.name = name.toStdString();
        if (!obj.value("color").isArray()) {
            error = err(loc, QStringLiteral("color must be an array of 3 numbers"));
            return false;
        }
        const QJsonArray colorArr = obj.value("color").toArray();
        if (colorArr.size() != 3) {
            error = err(loc, QStringLiteral("color must be an array of 3 numbers"));
            return false;
        }
        if (!asDouble(colorArr[0], m.r) || !asDouble(colorArr[1], m.g) || !asDouble(colorArr[2], m.b)) {
            error = err(loc, QStringLiteral("color entries must be numbers"));
            return false;
        }
        if (!asDouble(obj.value("opacity"), m.opacity)) {
            error = err(loc, QStringLiteral("opacity must be a number"));
            return false;
        }
        if (!parseMaterialTexture(obj.value("texture"), loc, m, error)) {
            return false;
        }
        ids.insert(m.id);
        outMaterials.push_back(std::move(m));
    }
    return true;
}

// materialIds: every id parseMaterials above just accepted -- front/back
// reference it directly (0 is always legal = "no material"; a nonzero value
// must name a parsed material, all-or-nothing).
bool parseMaterialAssignments(const QJsonValue& val, const std::unordered_set<geo::Id>& materialIds,
                               std::vector<ParsedMaterialAssignment>& out, QString& error) {
    if (!val.isArray()) {
        error = QStringLiteral("materialAssignments must be an array");
        return false;
    }
    const QJsonArray arr = val.toArray();
    out.reserve(static_cast<std::size_t>(arr.size()));
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const QString loc = idx(QStringLiteral("materialAssignments"), i);
        if (!arr[i].isArray()) {
            error = err(loc, QStringLiteral("must be an array"));
            return false;
        }
        const QJsonArray e = arr[i].toArray();
        // 4 (every pre-existing row, or an identity-transform row) or 5 (a
        // Position-Texture'd row) elements are both legal.
        if (e.size() != 4 && e.size() != 5) {
            error = err(loc, QStringLiteral("must be [kind, id, front, back] or [kind, id, front, back, {du,dv,rot,su,sv}]"));
            return false;
        }
        QString kindStr;
        if (!asString(e[0], kindStr)) {
            error = err(loc, QStringLiteral("kind must be a string"));
            return false;
        }
        geo::EntityKind kind{};
        if (!asKind(kindStr, kind)) {
            error = err(loc, QStringLiteral("invalid kind \"") + kindStr + QStringLiteral("\""));
            return false;
        }
        geo::Id id = 0;
        if (!asId(e[1], id)) {
            error = err(loc, QStringLiteral("id must be a positive integer"));
            return false;
        }
        geo::Id front = 0, back = 0;
        if (!asMaterialSlotId(e[2], front)) {
            error = err(loc, QStringLiteral("front must be a non-negative integer"));
            return false;
        }
        if (!asMaterialSlotId(e[3], back)) {
            error = err(loc, QStringLiteral("back must be a non-negative integer"));
            return false;
        }
        if (front != 0 && materialIds.count(front) == 0) {
            error = err(loc, QStringLiteral("unknown front material id ") + QString::number(front));
            return false;
        }
        if (back != 0 && materialIds.count(back) == 0) {
            error = err(loc, QStringLiteral("unknown back material id ") + QString::number(back));
            return false;
        }
        events::UvTransform uvTransform;  // identity unless a 5th element overrides it below
        if (e.size() == 5 && !parseUvTransform(e[4], loc, uvTransform, error)) {
            return false;
        }
        out.push_back(ParsedMaterialAssignment{EntityRef{kind, id}, front, back, uvTransform});
    }
    return true;
}

// Inverse of plr_writer.cpp's faceStyleToString. ok=false rejects the whole
// file (readDocument's all-or-nothing discipline), not a per-row skip.
events::FaceStyle faceStyleFromString(const QString& s, bool& ok) {
    ok = true;
    if (s == QStringLiteral("wireframe")) return events::FaceStyle::Wireframe;
    if (s == QStringLiteral("hiddenLine")) return events::FaceStyle::HiddenLine;
    if (s == QStringLiteral("shaded")) return events::FaceStyle::Shaded;
    if (s == QStringLiteral("shadedWithTextures")) return events::FaceStyle::ShadedWithTextures;
    if (s == QStringLiteral("monochrome")) return events::FaceStyle::Monochrome;
    if (s == QStringLiteral("xray")) return events::FaceStyle::XRay;
    ok = false;
    return events::FaceStyle::ShadedWithTextures;
}

// Inverse of plr_writer.cpp's projectionToString. Unlike faceStyle, the key
// itself may be absent entirely -- handled by parseCamera's caller, not here.
events::Projection projectionFromString(const QString& s, bool& ok) {
    ok = true;
    if (s == QStringLiteral("perspective")) return events::Projection::Perspective;
    if (s == QStringLiteral("parallel")) return events::Projection::Parallel;
    if (s == QStringLiteral("twoPoint")) return events::Projection::TwoPoint;
    ok = false;
    return events::Projection::Perspective;
}

bool asStyleColor(const QJsonValue& v, const QString& loc, agent::StyleColor& out, QString& error) {
    geo::Vec3 rgb;
    if (!asVec3(v, rgb)) {
        error = err(loc, QStringLiteral("must be an array of 3 numbers"));
        return false;
    }
    out = agent::StyleColor{rgb.x, rgb.y, rgb.z};
    return true;
}

// val is root.value("style"). Absent (isUndefined) leaves out.present false
// -- not an error. A present value must be a well-formed object with every field required.
bool parseStyle(const QJsonValue& val, ParsedStyle& out, QString& error) {
    if (val.isUndefined()) {
        return true;  // absent -- out.present stays false
    }
    if (!val.isObject()) {
        error = QStringLiteral("style must be an object");
        return false;
    }
    const QJsonObject obj = val.toObject();

    QString faceStyleStr;
    if (!asString(obj.value("faceStyle"), faceStyleStr)) {
        error = QStringLiteral("style.faceStyle must be a string");
        return false;
    }
    bool knownStyle = false;
    const events::FaceStyle faceStyle = faceStyleFromString(faceStyleStr, knownStyle);
    if (!knownStyle) {
        error = QStringLiteral("style.faceStyle: unknown value \"") + faceStyleStr + QStringLiteral("\"");
        return false;
    }

    bool profiles = false, depthCue = false, backEdges = false;
    if (!asBool(obj.value("profiles"), profiles)) {
        error = QStringLiteral("style.profiles must be a boolean");
        return false;
    }
    if (!asBool(obj.value("depthCue"), depthCue)) {
        error = QStringLiteral("style.depthCue must be a boolean");
        return false;
    }
    if (!asBool(obj.value("backEdges"), backEdges)) {
        error = QStringLiteral("style.backEdges must be a boolean");
        return false;
    }

    agent::StyleColor frontColor, backColor;
    if (!asStyleColor(obj.value("frontColor"), QStringLiteral("style.frontColor"), frontColor, error)) {
        return false;
    }
    if (!asStyleColor(obj.value("backColor"), QStringLiteral("style.backColor"), backColor, error)) {
        return false;
    }

    bool ambientOcclusion = false;
    if (!asBool(obj.value("ambientOcclusion"), ambientOcclusion)) {
        error = QStringLiteral("style.ambientOcclusion must be a boolean");
        return false;
    }
    double aoStrength = agent::kDefaultAoStrength;
    if (!asDouble(obj.value("aoStrength"), aoStrength)) {
        error = QStringLiteral("style.aoStrength must be a number");
        return false;
    }

    out = ParsedStyle{true, faceStyle, profiles, depthCue, backEdges, ambientOcclusion, aoStrength, frontColor, backColor};
    return true;
}

// val is root.value("shadows") -- same absent/well-formed contract as
// parseStyle. `enabled` renamed to `useSunForShading` (no back-compat
// parsing: `enabled` never shipped beyond this repo's test fixtures).
bool parseShadows(const QJsonValue& val, ParsedShadows& out, QString& error) {
    if (val.isUndefined()) {
        return true;  // absent -- out.present stays false
    }
    if (!val.isObject()) {
        error = QStringLiteral("shadows must be an object");
        return false;
    }
    const QJsonObject obj = val.toObject();

    bool useSunForShading = false, showShadows = false;
    if (!asBool(obj.value("useSunForShading"), useSunForShading)) {
        error = QStringLiteral("shadows.useSunForShading must be a boolean");
        return false;
    }
    if (!asBool(obj.value("showShadows"), showShadows)) {
        error = QStringLiteral("shadows.showShadows must be a boolean");
        return false;
    }

    double latitudeDeg = 0.0, longitudeDeg = 0.0, hourLocal = 0.0, light = 0.0, dark = 0.0;
    if (!asDouble(obj.value("latitudeDeg"), latitudeDeg)) {
        error = QStringLiteral("shadows.latitudeDeg must be a number");
        return false;
    }
    if (!asDouble(obj.value("longitudeDeg"), longitudeDeg)) {
        error = QStringLiteral("shadows.longitudeDeg must be a number");
        return false;
    }
    if (!asDouble(obj.value("hourLocal"), hourLocal)) {
        error = QStringLiteral("shadows.hourLocal must be a number");
        return false;
    }
    if (!asDouble(obj.value("light"), light)) {
        error = QStringLiteral("shadows.light must be a number");
        return false;
    }
    if (!asDouble(obj.value("dark"), dark)) {
        error = QStringLiteral("shadows.dark must be a number");
        return false;
    }

    int month = 0, day = 0;
    if (!asInt(obj.value("month"), month)) {
        error = QStringLiteral("shadows.month must be an integer");
        return false;
    }
    if (!asInt(obj.value("day"), day)) {
        error = QStringLiteral("shadows.day must be an integer");
        return false;
    }

    out = ParsedShadows{true, useSunForShading, showShadows, latitudeDeg, longitudeDeg, month, day, hourLocal, light, dark};
    return true;
}

// val is root.value("fog") -- same "absent is fine, present must be
// well-formed" contract parseStyle/parseShadows above follow.
bool parseFog(const QJsonValue& val, ParsedFog& out, QString& error) {
    if (val.isUndefined()) {
        return true;  // absent -- out.present stays false
    }
    if (!val.isObject()) {
        error = QStringLiteral("fog must be an object");
        return false;
    }
    const QJsonObject obj = val.toObject();

    bool enabled = false, useBackgroundColor = false;
    if (!asBool(obj.value("enabled"), enabled)) {
        error = QStringLiteral("fog.enabled must be a boolean");
        return false;
    }
    if (!asBool(obj.value("useBackgroundColor"), useBackgroundColor)) {
        error = QStringLiteral("fog.useBackgroundColor must be a boolean");
        return false;
    }

    double startDistance = 0.0, endDistance = 0.0;
    if (!asDouble(obj.value("startDistance"), startDistance)) {
        error = QStringLiteral("fog.startDistance must be a number");
        return false;
    }
    if (!asDouble(obj.value("endDistance"), endDistance)) {
        error = QStringLiteral("fog.endDistance must be a number");
        return false;
    }

    agent::StyleColor color;
    if (!asStyleColor(obj.value("color"), QStringLiteral("fog.color"), color, error)) {
        return false;
    }

    out = ParsedFog{true, enabled, startDistance, endDistance, useBackgroundColor, color};
    return true;
}

bool parseCamera(const QJsonValue& val, CameraState& out, QString& error) {
    if (!val.isObject()) {
        error = QStringLiteral("camera must be an object");
        return false;
    }
    const QJsonObject obj = val.toObject();
    if (!asVec3(obj.value("target"), out.target)) {
        error = QStringLiteral("camera.target must be 3 numbers");
        return false;
    }
    if (!asDouble(obj.value("azimuthDeg"), out.azimuthDeg)) {
        error = QStringLiteral("camera.azimuthDeg must be a number");
        return false;
    }
    if (!asDouble(obj.value("elevationDeg"), out.elevationDeg)) {
        error = QStringLiteral("camera.elevationDeg must be a number");
        return false;
    }
    if (!asDouble(obj.value("distance"), out.distance)) {
        error = QStringLiteral("camera.distance must be a number");
        return false;
    }
    if (!asDouble(obj.value("fovYDeg"), out.fovYDeg)) {
        error = QStringLiteral("camera.fovYDeg must be a number");
        return false;
    }
    // Optional-with-default, unlike every field above: absent -> Perspective.
    // A present-but-invalid value still rejects the whole file.
    if (obj.contains(QStringLiteral("projection"))) {
        QString projectionStr;
        if (!asString(obj.value("projection"), projectionStr)) {
            error = QStringLiteral("camera.projection must be a string");
            return false;
        }
        bool knownProjection = false;
        const events::Projection projection = projectionFromString(projectionStr, knownProjection);
        if (!knownProjection) {
            error = QStringLiteral("camera.projection: unknown value \"") + projectionStr + QStringLiteral("\"");
            return false;
        }
        out.projection = projection;
    } else {
        out.projection = events::Projection::Perspective;
    }
    return true;
}

bool parseMeta(const QJsonValue& val, DocumentMeta& out, QString& error) {
    if (!val.isObject()) {
        error = QStringLiteral("meta must be an object");
        return false;
    }
    const QJsonObject obj = val.toObject();
    if (!asString(obj.value("appVersion"), out.appVersion)) {
        error = QStringLiteral("meta.appVersion must be a string");
        return false;
    }
    if (!asString(obj.value("savedAt"), out.savedAt)) {
        error = QStringLiteral("meta.savedAt must be a string");
        return false;
    }
    if (!asString(obj.value("units"), out.units)) {
        error = QStringLiteral("meta.units must be a string");
        return false;
    }
    return true;
}

bool parseFormatHeader(const QJsonObject& root, QString& error) {
    QString format;
    if (!asString(root.value("format"), format)) {
        error = QStringLiteral("format field missing or not a string");
        return false;
    }
    const QString expected = QString::fromUtf8(kFormatName.data(), static_cast<qsizetype>(kFormatName.size()));
    if (format != expected) {
        error = QStringLiteral("format: expected \"") + expected + QStringLiteral("\", got \"") + format +
                QStringLiteral("\"");
        return false;
    }

    const QJsonValue versionVal = root.value("formatVersion");
    if (!versionVal.isDouble()) {
        error = QStringLiteral("formatVersion field missing or not a number");
        return false;
    }
    const double versionD = versionVal.toDouble();
    if (versionD != std::floor(versionD)) {
        error = QStringLiteral("formatVersion must be an integer");
        return false;
    }
    const int version = static_cast<int>(versionD);
    if (version > kFormatVersion) {
        error = QStringLiteral("file was written by a newer version");
        return false;
    }
    return true;
}

// Replays every parsed definition/instance through the geo::Model/geo::Scene
// restore APIs into scene (fresh, local). Required order: Scene::restoreNextId,
// then per definition restoreDefinition -> Model::restoreNextId ->
// restoreVertex/Edge/Face, then every instance last (so a forward-referencing
// definitionId is already in place). A false return signals an internal
// inconsistency, not a bad input file (already validated above).
bool buildStagingScene(const std::vector<ParsedDefinition>& defs, geo::Scene& scene, QString& error) {
    geo::Id maxSceneId = 0;
    for (const ParsedDefinition& def : defs) {
        maxSceneId = std::max(maxSceneId, def.id);
        for (const ParsedInstance& inst : def.instances) {
            maxSceneId = std::max(maxSceneId, inst.id);
        }
    }
    if (!scene.restoreNextId(maxSceneId + 1)) {
        error = QStringLiteral("internal inconsistency: Scene::restoreNextId failed");
        return false;
    }

    for (std::size_t i = 0; i < defs.size(); ++i) {
        const ParsedDefinition& def = defs[i];
        const QString defLoc = idx(QStringLiteral("definitions"), static_cast<qsizetype>(i));

        geo::Model* model = nullptr;
        if (def.id == geo::kRootDefinitionId) {
            // The root definition always exists (Scene's own ctor) -- there
            // is no restoreDefinition call for it (id 1 is reserved); only
            // its mesh is replayed, directly onto scene.root().model.
            model = &scene.root().model;
        } else {
            geo::Definition* d = scene.restoreDefinition(def.id, def.name, def.isGroup);
            if (d == nullptr) {
                error = err(defLoc, QStringLiteral("failed to restore definition (internal inconsistency)"));
                return false;
            }
            model = &d->model;
        }

        geo::Id maxMeshId = 0;
        for (const ParsedVertex& v : def.mesh.vertices) maxMeshId = std::max(maxMeshId, v.id);
        for (const ParsedEdge& e : def.mesh.edges) maxMeshId = std::max(maxMeshId, e.id);
        for (const ParsedFace& f : def.mesh.faces) maxMeshId = std::max(maxMeshId, f.id);
        if (!model->restoreNextId(maxMeshId + 1)) {
            error = err(defLoc, QStringLiteral("mesh: restoreNextId failed (internal inconsistency)"));
            return false;
        }
        for (std::size_t vi = 0; vi < def.mesh.vertices.size(); ++vi) {
            const ParsedVertex& v = def.mesh.vertices[vi];
            if (!model->restoreVertex(v.id, v.pos)) {
                error = err(idx(defLoc + QStringLiteral(".mesh.vertices"), static_cast<qsizetype>(vi)),
                            QStringLiteral("failed to restore vertex (internal inconsistency)"));
                return false;
            }
        }
        for (std::size_t ei = 0; ei < def.mesh.edges.size(); ++ei) {
            const ParsedEdge& e = def.mesh.edges[ei];
            if (!model->restoreEdge(e.id, e.v0, e.v1)) {
                error = err(idx(defLoc + QStringLiteral(".mesh.edges"), static_cast<qsizetype>(ei)),
                            QStringLiteral("failed to restore edge (internal inconsistency)"));
                return false;
            }
        }
        for (std::size_t fi = 0; fi < def.mesh.faces.size(); ++fi) {
            const ParsedFace& f = def.mesh.faces[fi];
            if (!model->restoreFace(f.id, f.loop)) {
                error = err(idx(defLoc + QStringLiteral(".mesh.faces"), static_cast<qsizetype>(fi)),
                            QStringLiteral("failed to restore face (internal inconsistency)"));
                return false;
            }
        }
    }

    for (std::size_t i = 0; i < defs.size(); ++i) {
        const ParsedDefinition& def = defs[i];
        for (std::size_t j = 0; j < def.instances.size(); ++j) {
            const ParsedInstance& inst = def.instances[j];
            if (!scene.restoreInstance(def.id, inst.id, inst.definitionId, inst.transform, inst.name)) {
                const QString loc = idx(idx(QStringLiteral("definitions"), static_cast<qsizetype>(i)) +
                                             QStringLiteral(".instances"),
                                         static_cast<qsizetype>(j));
                error = err(loc, QStringLiteral("failed to restore instance (internal inconsistency)"));
                return false;
            }
        }
    }
    return true;
}

// Runs every parseXxx() helper above, in schema order, filling doc and
// stagingScene. Returns false (with error set) on the first problem found --
// nothing under this function ever touches an agent argument.
bool parseDocument(const QJsonObject& root, ParsedDocument& doc, geo::Scene& stagingScene, QString& error) {
    if (!parseFormatHeader(root, error)) {
        return false;
    }
    if (!parseMeta(root.value("meta"), doc.meta, error)) {
        return false;
    }
    if (!root.value("definitions").isArray()) {
        error = QStringLiteral("definitions must be an array");
        return false;
    }
    if (!parseDefinitions(root.value("definitions").toArray(), doc.definitions, error)) {
        return false;
    }

    if (!parseTags(root.value("tags"), doc.tags, error)) {
        return false;
    }
    std::unordered_set<std::uint64_t> tagIds;
    for (const Tag& t : doc.tags) {
        tagIds.insert(t.id);
    }
    if (!parseTagAssignments(root.value("tagAssignments"), tagIds, doc.assignments, error)) {
        return false;
    }
    if (!parseHidden(root.value("hidden"), doc.hidden, error)) {
        return false;
    }
    if (!parseGuides(root.value("guides"), doc.guides, error)) {
        return false;
    }

    std::unordered_set<geo::Id> annotationIds;
    if (!parseDimensions(root.value("dimensions"), annotationIds, doc.dimensions, error)) {
        return false;
    }
    if (!parseTexts(root.value("texts"), annotationIds, doc.texts, error)) {
        return false;
    }

    if (!parseSectionPlanes(root.value("sectionPlanes"), doc.sectionPlanes, error)) {
        return false;
    }
    if (!parseAxes(root.value("axes"), doc.axesFrame, error)) {
        return false;
    }

    if (!parseMaterials(root.value("materials"), doc.materials, error)) {
        return false;
    }
    std::unordered_set<geo::Id> materialIds;
    for (const Material& m : doc.materials) {
        materialIds.insert(m.id);
    }
    const QJsonValue materialAssignmentsVal = root.value("materialAssignments");
    if (!materialAssignmentsVal.isUndefined()) {
        // Present (the writer always emits it alongside a non-null
        // `materials`) -- absent is tolerated too and simply leaves
        // doc.materialAssignments empty.
        if (!parseMaterialAssignments(materialAssignmentsVal, materialIds, doc.materialAssignments, error)) {
            return false;
        }
    }

    if (!parseStyle(root.value("style"), doc.style, error)) {
        return false;
    }

    if (!parseShadows(root.value("shadows"), doc.shadows, error)) {
        return false;
    }

    if (!parseFog(root.value("fog"), doc.fog, error)) {
        return false;
    }

    if (!parseCamera(root.value("camera"), doc.camera, error)) {
        return false;
    }

    return buildStagingScene(doc.definitions, stagingScene, error);
}

}  // namespace

// Failure in phases (a)/(b) leaves every agent argument untouched. A failure
// signaled mid phase (c) is a programming-error case this does NOT roll back from.
ReadResult readDocument(const QByteArray& bytes, agent::GeometryApi& geometry, agent::TagStore& tags,
                         agent::GuideStore& guides, agent::AnnotationStore& annotations,
                         agent::SectionStore& sections, agent::AxesStore& axes, agent::MaterialRepository& materials,
                         agent::AssetRepository& assets, agent::StyleStore& style, agent::ShadowStore& shadows,
                         agent::FogStore& fog, CameraState* cameraOut, DocumentMeta* metaOut) {
    const Container container = sniffContainer(bytes);
    if (container == Container::Unknown) {
        return ReadResult{false, QStringLiteral("unrecognized file content (not a .plr JSON document)")};
    }

    // openContainer does raw ZIP extraction only; jsonBytes then feeds the
    // same parse/validate path RawJson bytes already use.
    QByteArray jsonBytes;
    std::unordered_map<std::string, agent::Asset> containerAssets;  // stays empty for RawJson
    if (container == Container::Zip) {
        const OpenedContainer opened = openContainer(bytes);
        if (!opened.ok) {
            return ReadResult{false, opened.error};
        }
        jsonBytes = opened.jsonBytes;
        containerAssets = std::move(opened.assets);
    } else {
        jsonBytes = bytes;
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        const QString msg = QStringLiteral("JSON parse error at offset ") + QString::number(parseError.offset) +
                             QStringLiteral(": ") + parseError.errorString();
        return ReadResult{false, msg};
    }
    if (!jsonDoc.isObject()) {
        return ReadResult{false, QStringLiteral("top-level JSON value is not an object")};
    }

    ParsedDocument parsed;
    geo::Scene stagingScene;
    QString error;
    if (!parseDocument(jsonDoc.object(), parsed, stagingScene, error)) {
        return ReadResult{false, error};
    }

    // Cross-validates every material's non-empty assetHash against the ZIP's
    // own asset entries before anything is applied to any agent, reusing
    // restoreAsset's own hash-verification check. RawJson always leaves
    // containerAssets empty, so a hand-crafted assetHash with no ZIP is left
    // unresolved (same "stale reference is legal" stance as
    // tagAssignments/hidden), not rejected.
    agent::AssetRepository stagingAssets;
    if (container == Container::Zip) {
        for (auto& [hash, asset] : containerAssets) {
            if (!stagingAssets.restoreAsset(hash, asset.bytes, asset.ext)) {
                return ReadResult{false, QStringLiteral("ZIP archive: asset \"") + QString::fromStdString(hash) +
                                              QStringLiteral("\" content does not match its own hash")};
            }
        }
        for (std::size_t i = 0; i < parsed.materials.size(); ++i) {
            const Material& m = parsed.materials[i];
            if (!m.assetHash.empty() && stagingAssets.get(m.assetHash) == nullptr) {
                const QString loc = idx(QStringLiteral("materials"), static_cast<qsizetype>(i));
                return ReadResult{false, err(loc, QStringLiteral("texture.assetHash references unknown ZIP asset \"") +
                                                       QString::fromStdString(m.assetHash) + QStringLiteral("\""))};
            }
        }
    }

    // -- Phase (c): apply. Everything above validated successfully, so every
    // restore* call below is expected to succeed.
    geometry.clearForRestore();
    tags.clearForRestore();
    guides.clearForRestore();
    annotations.clearForRestore();
    sections.clearForRestore();
    materials.clearForRestore();
    assets.clearForRestore();
    style.clearForRestore();
    shadows.clearForRestore();
    fog.clearForRestore();

    for (const std::string& hash : stagingAssets.hashes()) {
        const agent::Asset* a = stagingAssets.get(hash);
        if (a == nullptr || !assets.restoreAsset(hash, a->bytes, a->ext)) {
            return ReadResult{false, QStringLiteral("internal inconsistency: failed to restore a validated asset")};
        }
    }

    geometry.adoptScene(std::move(stagingScene));
    geometry.restoreHidden(std::move(parsed.hidden));

    for (Tag& t : parsed.tags) {
        if (!tags.restoreTag(t)) {
            return ReadResult{false, QStringLiteral("internal inconsistency: failed to restore a validated tag")};
        }
    }
    for (ParsedAssignment& a : parsed.assignments) {
        if (!tags.restoreAssignment(a.ref, a.tagId)) {
            return ReadResult{false,
                               QStringLiteral("internal inconsistency: failed to restore a validated tag assignment")};
        }
    }
    for (Guide& g : parsed.guides) {
        if (!guides.restoreGuide(g)) {
            return ReadResult{false, QStringLiteral("internal inconsistency: failed to restore a validated guide")};
        }
    }
    for (Dimension& d : parsed.dimensions) {
        if (!annotations.restoreDimension(d)) {
            return ReadResult{false,
                               QStringLiteral("internal inconsistency: failed to restore a validated dimension")};
        }
    }
    for (TextNote& t : parsed.texts) {
        if (!annotations.restoreTextNote(t)) {
            return ReadResult{false,
                               QStringLiteral("internal inconsistency: failed to restore a validated text note")};
        }
    }
    for (SectionPlane& p : parsed.sectionPlanes) {
        if (!sections.restorePlane(p)) {
            return ReadResult{
                false, QStringLiteral("internal inconsistency: failed to restore a validated section plane")};
        }
    }
    axes.restoreFrame(parsed.axesFrame);

    for (Material& m : parsed.materials) {
        if (!materials.restoreMaterial(m)) {
            return ReadResult{false, QStringLiteral("internal inconsistency: failed to restore a validated material")};
        }
    }
    for (ParsedMaterialAssignment& a : parsed.materialAssignments) {
        if (!materials.restoreAssignment(a.ref, a.front, a.back, a.uvTransform)) {
            return ReadResult{
                false, QStringLiteral("internal inconsistency: failed to restore a validated material assignment")};
        }
    }

    // parsed.style.present is false for every file with no `style` key --
    // style was already reset to its ctor-seeded default by
    // clearForRestore() above, so there's nothing further to do.
    if (parsed.style.present) {
        style.restoreFaceStyle(parsed.style.faceStyle);
        style.restoreEdgeFlags(parsed.style.profiles, parsed.style.depthCue, parsed.style.backEdges);
        style.restoreAmbientOcclusion(parsed.style.ambientOcclusion, parsed.style.aoStrength);
        style.restoreColors(parsed.style.frontColor, parsed.style.backColor);
    }

    // Same "reader defaults on absence" contract as `style` above.
    if (parsed.shadows.present) {
        shadows.restoreUseSunForShading(parsed.shadows.useSunForShading);
        shadows.restoreShowShadows(parsed.shadows.showShadows);
        shadows.restorePosition(parsed.shadows.latitudeDeg, parsed.shadows.longitudeDeg);
        shadows.restoreDateTime(parsed.shadows.month, parsed.shadows.day, parsed.shadows.hourLocal);
        shadows.restoreLight(parsed.shadows.light);
        shadows.restoreDark(parsed.shadows.dark);
    }

    // Same "reader defaults on absence" contract as `style`/`shadows` above.
    if (parsed.fog.present) {
        fog.restoreEnabled(parsed.fog.enabled);
        fog.restoreRange(parsed.fog.startDistance, parsed.fog.endDistance);
        fog.restoreUseBackgroundColor(parsed.fog.useBackgroundColor);
        fog.restoreColor(parsed.fog.color.r, parsed.fog.color.g, parsed.fog.color.b);
    }

    // Recomputes associated/lastA/lastB against the just-restored root model
    // -- AnnotationStore::refreshAssociations' own contract, consistent with
    // how GeometryChangedCommand drives it at runtime.
    annotations.refreshAssociations(geometry.model());

    if (cameraOut != nullptr) {
        *cameraOut = parsed.camera;
    }
    if (metaOut != nullptr) {
        *metaOut = parsed.meta;
    }

    return ReadResult{true, QString()};
}

}  // namespace plnr::io
