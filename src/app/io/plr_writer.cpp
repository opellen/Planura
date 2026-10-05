#include "io/plr_writer.h"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>

#include "agent/events.h"
#include "io/plr_format.h"

namespace plnr::io {

namespace {

QJsonArray vec3ToJson(const geo::Vec3& v) {
    return QJsonArray{v.x, v.y, v.z};
}

// EntityKind -> lowercase "kind" string. Literal switch, so reordering enumerators can't break it.
QString kindToString(geo::EntityKind kind) {
    switch (kind) {
        case geo::EntityKind::Vertex:
            return QStringLiteral("vertex");
        case geo::EntityKind::Edge:
            return QStringLiteral("edge");
        case geo::EntityKind::Face:
            return QStringLiteral("face");
        case geo::EntityKind::Instance:
            return QStringLiteral("instance");
    }
    return QString();  // unreachable
}

// Sort key for tagAssignments/hidden: ascending by (kindString, id).
bool entityRefLess(const events::EntityRef& a, const events::EntityRef& b) {
    const QString ka = kindToString(a.kind);
    const QString kb = kindToString(b.kind);
    if (ka != kb) {
        return ka < kb;
    }
    return a.id < b.id;
}

// An edge's two endpoint vertex ids, via its half-edges' origins.
std::pair<geo::Id, geo::Id> edgeEndpoints(const geo::Model& model, const geo::Edge& edge) {
    const geo::HalfEdge* h0 = model.halfEdge(edge.halfEdges[0]);
    const geo::HalfEdge* h1 = model.halfEdge(edge.halfEdges[1]);
    return {h0 != nullptr ? h0->origin : geo::kInvalidId, h1 != nullptr ? h1->origin : geo::kInvalidId};
}

// {vertices, edges, faces}, each sorted by id ascending; HalfEdges are not serialized.
// A face's "loop" is faceVertexLoop's result verbatim, never rotated (the reader rebuilds winding from it).
QJsonObject buildMesh(const geo::Model& model) {
    std::vector<const geo::Vertex*> vertices;
    vertices.reserve(model.vertices().size());
    for (const auto& [id, v] : model.vertices()) {
        vertices.push_back(&v);
    }
    std::sort(vertices.begin(), vertices.end(), [](const geo::Vertex* a, const geo::Vertex* b) { return a->id < b->id; });

    QJsonArray verticesJson;
    for (const geo::Vertex* v : vertices) {
        verticesJson.append(QJsonArray{static_cast<double>(v->id), v->pos.x, v->pos.y, v->pos.z});
    }

    std::vector<const geo::Edge*> edges;
    edges.reserve(model.edges().size());
    for (const auto& [id, e] : model.edges()) {
        edges.push_back(&e);
    }
    std::sort(edges.begin(), edges.end(), [](const geo::Edge* a, const geo::Edge* b) { return a->id < b->id; });

    QJsonArray edgesJson;
    for (const geo::Edge* e : edges) {
        const auto [v0, v1] = edgeEndpoints(model, *e);
        edgesJson.append(QJsonArray{static_cast<double>(e->id), static_cast<double>(v0), static_cast<double>(v1)});
    }

    std::vector<const geo::Face*> faces;
    faces.reserve(model.faces().size());
    for (const auto& [id, f] : model.faces()) {
        faces.push_back(&f);
    }
    std::sort(faces.begin(), faces.end(), [](const geo::Face* a, const geo::Face* b) { return a->id < b->id; });

    QJsonArray facesJson;
    for (const geo::Face* f : faces) {
        QJsonArray loop;
        for (geo::Id vertexId : model.faceVertexLoop(f->id)) {
            loop.append(static_cast<double>(vertexId));
        }
        QJsonObject faceObj;
        faceObj["id"] = static_cast<double>(f->id);
        faceObj["loop"] = loop;
        facesJson.append(faceObj);
    }

    QJsonObject mesh;
    mesh["vertices"] = verticesJson;
    mesh["edges"] = edgesJson;
    mesh["faces"] = facesJson;
    return mesh;
}

// Children keep Definition::children order (meaningful) -- NOT sorted.
QJsonArray buildInstancesArray(const std::vector<geo::Instance>& children) {
    QJsonArray out;
    for (const geo::Instance& inst : children) {
        const QJsonArray transform{
            inst.transform.col0.x, inst.transform.col0.y, inst.transform.col0.z,
            inst.transform.col1.x, inst.transform.col1.y, inst.transform.col1.z,
            inst.transform.col2.x, inst.transform.col2.y, inst.transform.col2.z,
            inst.transform.t.x,    inst.transform.t.y,    inst.transform.t.z,
        };
        QJsonObject obj;
        obj["id"] = static_cast<double>(inst.id);
        obj["definitionId"] = static_cast<double>(inst.definitionId);
        obj["name"] = QString::fromStdString(inst.name);
        obj["transform"] = transform;
        out.append(obj);
    }
    return out;
}

QJsonObject buildDefinitionObject(const geo::Definition& def) {
    QJsonObject obj;
    obj["id"] = static_cast<double>(def.id);
    obj["name"] = QString::fromStdString(def.name);
    obj["isGroup"] = def.isGroup;
    obj["mesh"] = buildMesh(def.model);
    obj["instances"] = buildInstancesArray(def.children);
    return obj;
}

// Depth-first walk from defId via children's Instance::definitionId (visited set guards cycles).
// Definitions with no instances are unreachable and not written.
void collectReachableDefinitions(const geo::Scene& scene, geo::Id defId, std::unordered_set<geo::Id>& visited,
                                  std::vector<geo::Id>& order) {
    if (!visited.insert(defId).second) {
        return;
    }
    order.push_back(defId);
    const geo::Definition* def = scene.definition(defId);
    if (def == nullptr) {
        return;  // defensive -- every id reaching here named a live child when discovered
    }
    for (const geo::Instance& inst : def->children) {
        collectReachableDefinitions(scene, inst.definitionId, visited, order);
    }
}

// Sorted by id ascending -- root (id 1) naturally sorts first.
QJsonArray buildDefinitionsArray(const geo::Scene& scene) {
    std::unordered_set<geo::Id> visited;
    std::vector<geo::Id> order;
    collectReachableDefinitions(scene, geo::kRootDefinitionId, visited, order);
    std::sort(order.begin(), order.end());

    QJsonArray out;
    for (geo::Id id : order) {
        const geo::Definition* def = scene.definition(id);
        if (def == nullptr) {
            continue;  // defensive, see collectReachableDefinitions
        }
        out.append(buildDefinitionObject(*def));
    }
    return out;
}

QJsonArray buildTagsArray(const agent::TagStore& tags) {
    std::vector<const agent::Tag*> sorted;
    sorted.reserve(tags.tags().size());
    for (const agent::Tag& tag : tags.tags()) {
        sorted.push_back(&tag);
    }
    std::sort(sorted.begin(), sorted.end(), [](const agent::Tag* a, const agent::Tag* b) { return a->id < b->id; });

    QJsonArray out;
    for (const agent::Tag* tag : sorted) {
        QJsonObject obj;
        obj["id"] = static_cast<double>(tag->id);
        obj["name"] = QString::fromStdString(tag->name);
        obj["visible"] = tag->visible;
        out.append(obj);
    }
    return out;
}

// Verbatim, sorted by (kindString, id); stale entries (EntityRef no longer resolves) are kept.
QJsonArray buildTagAssignmentsArray(const agent::TagStore& tags) {
    std::vector<std::pair<events::EntityRef, std::uint64_t>> entries(tags.assignments().begin(), tags.assignments().end());
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return entityRefLess(a.first, b.first); });

    QJsonArray out;
    for (const auto& [ref, tagId] : entries) {
        out.append(QJsonArray{kindToString(ref.kind), static_cast<double>(ref.id), static_cast<double>(tagId)});
    }
    return out;
}

// Same verbatim/stale-kept contract as buildTagAssignmentsArray.
QJsonArray buildHiddenArray(const agent::GeometryApi& geometry) {
    std::vector<events::EntityRef> refs(geometry.hidden().begin(), geometry.hidden().end());
    std::sort(refs.begin(), refs.end(), entityRefLess);

    QJsonArray out;
    for (const events::EntityRef& ref : refs) {
        out.append(QJsonArray{kindToString(ref.kind), static_cast<double>(ref.id)});
    }
    return out;
}

QJsonArray buildGuidesArray(const agent::GuideStore& guides) {
    std::vector<const agent::Guide*> sorted;
    sorted.reserve(guides.guides().size());
    for (const agent::Guide& guide : guides.guides()) {
        sorted.push_back(&guide);
    }
    std::sort(sorted.begin(), sorted.end(), [](const agent::Guide* a, const agent::Guide* b) { return a->id < b->id; });

    QJsonArray out;
    for (const agent::Guide* guide : sorted) {
        QJsonObject obj;
        obj["id"] = static_cast<double>(guide->id);
        obj["isLine"] = guide->isLine;
        obj["point"] = vec3ToJson(guide->point);
        obj["dir"] = vec3ToJson(guide->dir);
        obj["hidden"] = guide->hidden;
        out.append(obj);
    }
    return out;
}

QJsonArray buildDimensionsArray(const agent::AnnotationStore& annotations) {
    std::vector<const agent::Dimension*> sorted;
    sorted.reserve(annotations.dimensions().size());
    for (const agent::Dimension& dim : annotations.dimensions()) {
        sorted.push_back(&dim);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const agent::Dimension* a, const agent::Dimension* b) { return a->id < b->id; });

    QJsonArray out;
    for (const agent::Dimension* dim : sorted) {
        QJsonObject obj;
        obj["id"] = static_cast<double>(dim->id);
        obj["vertexA"] = static_cast<double>(dim->vertexA);
        obj["vertexB"] = static_cast<double>(dim->vertexB);
        obj["offsetDir"] = vec3ToJson(dim->offsetDir);
        obj["offset"] = dim->offset;
        obj["overrideText"] = QString::fromStdString(dim->overrideText);
        obj["associated"] = dim->associated;
        obj["lastA"] = vec3ToJson(dim->lastA);
        obj["lastB"] = vec3ToJson(dim->lastB);
        out.append(obj);
    }
    return out;
}

QJsonArray buildTextsArray(const agent::AnnotationStore& annotations) {
    std::vector<const agent::TextNote*> sorted;
    sorted.reserve(annotations.texts().size());
    for (const agent::TextNote& text : annotations.texts()) {
        sorted.push_back(&text);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const agent::TextNote* a, const agent::TextNote* b) { return a->id < b->id; });

    QJsonArray out;
    for (const agent::TextNote* text : sorted) {
        QJsonObject obj;
        obj["id"] = static_cast<double>(text->id);
        obj["screenFixed"] = text->screenFixed;
        obj["screenX"] = text->screenX;
        obj["screenY"] = text->screenY;
        obj["worldAnchor"] = vec3ToJson(text->worldAnchor);
        if (text->leaderTarget.has_value()) {
            obj["leaderTarget"] =
                QJsonArray{kindToString(text->leaderTarget->kind), static_cast<double>(text->leaderTarget->id)};
        } else {
            obj["leaderTarget"] = QJsonValue();  // null
        }
        obj["text"] = QString::fromStdString(text->text);
        out.append(obj);
    }
    return out;
}

QJsonArray buildSectionPlanesArray(const agent::SectionStore& sections) {
    std::vector<const agent::SectionPlane*> sorted;
    sorted.reserve(sections.planes().size());
    for (const agent::SectionPlane& plane : sections.planes()) {
        sorted.push_back(&plane);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const agent::SectionPlane* a, const agent::SectionPlane* b) { return a->id < b->id; });

    QJsonArray out;
    for (const agent::SectionPlane* plane : sorted) {
        QJsonObject obj;
        obj["id"] = static_cast<double>(plane->id);
        obj["name"] = QString::fromStdString(plane->name);
        obj["point"] = vec3ToJson(plane->point);
        obj["normal"] = vec3ToJson(plane->normal);
        obj["active"] = plane->active;
        obj["hidden"] = plane->hidden;
        out.append(obj);
    }
    return out;
}

// Sorted by id ascending. pbr is reserved, always null. texture: null when
// m->assetHash is empty, else {assetHash, tileW, tileH}.
QJsonArray buildMaterialsArray(const agent::MaterialRepository& materials) {
    std::vector<const agent::Material*> sorted;
    sorted.reserve(materials.materials().size());
    for (const agent::Material& m : materials.materials()) {
        sorted.push_back(&m);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const agent::Material* a, const agent::Material* b) { return a->id < b->id; });

    QJsonArray out;
    for (const agent::Material* m : sorted) {
        QJsonObject obj;
        obj["id"] = static_cast<double>(m->id);
        obj["name"] = QString::fromStdString(m->name);
        obj["color"] = QJsonArray{m->r, m->g, m->b};
        obj["opacity"] = m->opacity;
        if (m->assetHash.empty()) {
            obj["texture"] = QJsonValue();  // untextured
        } else {
            QJsonObject texture;
            texture["assetHash"] = QString::fromStdString(m->assetHash);
            texture["tileW"] = m->tileW;
            texture["tileH"] = m->tileH;
            obj["texture"] = texture;
        }
        obj["pbr"] = QJsonValue();  // reserved
        out.append(obj);
    }
    return out;
}

// Verbatim, sorted by (kindString, id), stale entries kept. 0 in either slot = unassigned.
// A 5th element {du,dv,rot,su,sv} is appended only when uvTransform is non-identity.
QJsonArray buildMaterialAssignmentsArray(const agent::MaterialRepository& materials) {
    std::vector<std::pair<events::EntityRef, agent::MaterialAssignment>> entries(materials.assignments().begin(),
                                                                                    materials.assignments().end());
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return entityRefLess(a.first, b.first); });

    QJsonArray out;
    for (const auto& [ref, assign] : entries) {
        QJsonArray row{kindToString(ref.kind), static_cast<double>(ref.id),
                       static_cast<double>(assign.frontMaterialId), static_cast<double>(assign.backMaterialId)};
        if (!events::isIdentityUvTransform(assign.uvTransform)) {
            QJsonObject transform;
            transform["du"] = assign.uvTransform.offsetU;
            transform["dv"] = assign.uvTransform.offsetV;
            transform["rot"] = assign.uvTransform.rotationRad;
            transform["su"] = assign.uvTransform.scaleU;
            transform["sv"] = assign.uvTransform.scaleV;
            row.append(transform);
        }
        out.append(row);
    }
    return out;
}

// events::FaceStyle -> lowerCamelCase string (literal switch).
QString faceStyleToString(events::FaceStyle style) {
    switch (style) {
        case events::FaceStyle::Wireframe:
            return QStringLiteral("wireframe");
        case events::FaceStyle::HiddenLine:
            return QStringLiteral("hiddenLine");
        case events::FaceStyle::Shaded:
            return QStringLiteral("shaded");
        case events::FaceStyle::ShadedWithTextures:
            return QStringLiteral("shadedWithTextures");
        case events::FaceStyle::Monochrome:
            return QStringLiteral("monochrome");
        case events::FaceStyle::XRay:
            return QStringLiteral("xray");
    }
    return QStringLiteral("shadedWithTextures");  // unreachable
}

// events::Projection -> lowerCamelCase string (literal switch).
QString projectionToString(events::Projection projection) {
    switch (projection) {
        case events::Projection::Perspective:
            return QStringLiteral("perspective");
        case events::Projection::Parallel:
            return QStringLiteral("parallel");
        case events::Projection::TwoPoint:
            return QStringLiteral("twoPoint");
    }
    return QStringLiteral("perspective");  // unreachable
}

// Present only when style.isAllDefault() is false.
QJsonObject buildStyleObject(const agent::StyleStore& style) {
    QJsonObject obj;
    obj["faceStyle"] = faceStyleToString(style.faceStyle());
    obj["profiles"] = style.profiles();
    obj["depthCue"] = style.depthCue();
    obj["backEdges"] = style.backEdges();
    obj["frontColor"] = QJsonArray{style.defaultFrontColor().r, style.defaultFrontColor().g, style.defaultFrontColor().b};
    obj["backColor"] = QJsonArray{style.defaultBackColor().r, style.defaultBackColor().g, style.defaultBackColor().b};
    obj["ambientOcclusion"] = style.ambientOcclusion();
    obj["aoStrength"] = style.aoStrength();
    return obj;
}

// Present only when shadows.isAllDefault() is false.
QJsonObject buildShadowsObject(const agent::ShadowStore& shadows) {
    QJsonObject obj;
    obj["useSunForShading"] = shadows.useSunForShading();
    obj["showShadows"] = shadows.showShadows();
    obj["latitudeDeg"] = shadows.latitudeDeg();
    obj["longitudeDeg"] = shadows.longitudeDeg();
    obj["month"] = shadows.month();
    obj["day"] = shadows.day();
    obj["hourLocal"] = shadows.hourLocal();
    obj["light"] = shadows.light();
    obj["dark"] = shadows.dark();
    return obj;
}

// Present only when fog.isAllDefault() is false. colorR/G/B are reserved custom-color fields.
QJsonObject buildFogObject(const agent::FogStore& fog) {
    QJsonObject obj;
    obj["enabled"] = fog.enabled();
    obj["startDistance"] = fog.startDistance();
    obj["endDistance"] = fog.endDistance();
    obj["useBackgroundColor"] = fog.useBackgroundColor();
    obj["color"] = QJsonArray{fog.colorR(), fog.colorG(), fog.colorB()};
    return obj;
}

QJsonObject buildAxesObject(const agent::AxesStore& axes) {
    const agent::Frame& frame = axes.frame();
    QJsonObject obj;
    obj["origin"] = vec3ToJson(frame.origin);
    obj["x"] = vec3ToJson(frame.xDir);
    obj["y"] = vec3ToJson(frame.yDir);
    obj["z"] = vec3ToJson(frame.zDir);
    return obj;
}

QJsonObject buildCameraObject(const CameraState& camera) {
    QJsonObject obj;
    obj["target"] = vec3ToJson(camera.target);
    obj["azimuthDeg"] = camera.azimuthDeg;
    obj["elevationDeg"] = camera.elevationDeg;
    obj["distance"] = camera.distance;
    obj["fovYDeg"] = camera.fovYDeg;
    // Always written; reader treats an absent projection as Perspective.
    obj["projection"] = projectionToString(camera.projection);
    return obj;
}

QJsonObject buildMetaObject(const DocumentMeta& meta) {
    QJsonObject obj;
    obj["appVersion"] = meta.appVersion;
    obj["savedAt"] = meta.savedAt;
    obj["units"] = meta.units;
    return obj;
}

}  // namespace

QJsonDocument writeDocument(const agent::GeometryApi& geometry, const agent::TagStore& tags,
                             const agent::GuideStore& guides, const agent::AnnotationStore& annotations,
                             const agent::SectionStore& sections, const agent::AxesStore& axes,
                             const agent::MaterialRepository& materials, const agent::StyleStore& style,
                             const agent::ShadowStore& shadows, const agent::FogStore& fog, const CameraState& camera,
                             const DocumentMeta& meta) {
    QJsonObject root;
    root["format"] = QString::fromUtf8(kFormatName.data(), static_cast<qsizetype>(kFormatName.size()));
    root["formatVersion"] = kFormatVersion;
    root["meta"] = buildMetaObject(meta);
    root["definitions"] = buildDefinitionsArray(geometry.scene());
    root["tags"] = buildTagsArray(tags);
    root["tagAssignments"] = buildTagAssignmentsArray(tags);
    root["hidden"] = buildHiddenArray(geometry);
    root["guides"] = buildGuidesArray(guides);
    root["dimensions"] = buildDimensionsArray(annotations);
    root["texts"] = buildTextsArray(annotations);
    root["sectionPlanes"] = buildSectionPlanesArray(sections);
    root["axes"] = buildAxesObject(axes);
    root["camera"] = buildCameraObject(camera);
    root["curves"] = QJsonValue();  // reserved
    if (materials.materials().empty()) {
        // Empty MaterialRepository: `materials: null`, `materialAssignments` omitted.
        root["materials"] = QJsonValue();
    } else {
        root["materials"] = buildMaterialsArray(materials);
        root["materialAssignments"] = buildMaterialAssignmentsArray(materials);
    }
    // style/shadows/fog are each OMITTED entirely (not `null`) when all-default.
    if (!style.isAllDefault()) {
        root["style"] = buildStyleObject(style);
    }
    if (!shadows.isAllDefault()) {
        root["shadows"] = buildShadowsObject(shadows);
    }
    if (!fog.isAllDefault()) {
        root["fog"] = buildFogObject(fog);
    }
    return QJsonDocument(root);
}

}  // namespace plnr::io
