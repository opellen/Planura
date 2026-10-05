#include "io/obj_writer.h"

#include <algorithm>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QString>
#include <QStringList>

#include <geo/entity.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/triangulate.h>
#include <geo/vec3.h>

#include "agent/events.h"

namespace plnr::io {

namespace {

// See obj_writer.h's Axis/units section (currently identity -- exact
// inverse of obj_reader.cpp's fromObjSpace). Kept as a real function so a
// future "Swap YZ" option is a one-line change.
geo::Vec3 toObjSpace(const geo::Vec3& internalZUp) {
    return internalZUp;
}

// 'g',17: exact IEEE-754 double round-trip, shortest representation (Qt
// trims trailing zeros). "-0" folded to "0" for byte-stable goldens.
QString formatDouble(double v) {
    QString s = QString::number(v, 'g', 17);
    if (s == QStringLiteral("-0")) {
        return QStringLiteral("0");
    }
    return s;
}

QString vecToObjLine(const char* tag, const geo::Vec3& v) {
    return QString::fromLatin1(tag) + QStringLiteral(" ") + formatDouble(v.x) + QStringLiteral(" ") +
           formatDouble(v.y) + QStringLiteral(" ") + formatDouble(v.z);
}

// OBJ `g` names are whitespace-delimited -- spaces become underscores,
// matching the reference modeler's own OBJ-file-name substitution rule.
QString sanitizeGroupNamePart(const std::string& raw) {
    QString s = QString::fromStdString(raw);
    s.replace(QChar(' '), QChar('_'));
    return s;
}

// One `g <path>` worth of exportable state: which Model to read from, the
// world transform baking its local coordinates into world space, and the
// already visibility-filtered, id-sorted list of face ids to include.
struct ObjGroup {
    QString path;
    const geo::Model* model{};
    geo::Transform worldTransform;
    std::vector<geo::Id> faceIds;
};

// Depth-first walk from defId (see obj_writer.h's Structure/Visibility
// sections). applyVisibilityFilter is true only for the initial root call --
// recursive calls always pass false (visibility isn't exposed below root).
void collectGroups(const geo::Scene& scene, geo::Id defId, const QString& path, const geo::Transform& worldTransform,
                    const agent::GeometryApi& geometry, const agent::TagStore& tags, bool applyVisibilityFilter,
                    std::vector<ObjGroup>& out) {
    const geo::Definition* def = scene.definition(defId);
    if (def == nullptr) {
        return;  // defensive -- every id reaching here named a live child when discovered
    }

    std::vector<geo::Id> faceIds;
    faceIds.reserve(def->model.faces().size());
    for (const auto& [id, face] : def->model.faces()) {
        (void)face;
        if (applyVisibilityFilter) {
            const events::EntityRef ref{geo::EntityKind::Face, id};
            if (geometry.isHidden(ref) || !tags.isEntityVisible(ref)) {
                continue;
            }
        }
        faceIds.push_back(id);
    }
    std::sort(faceIds.begin(), faceIds.end());
    out.push_back(ObjGroup{path, &def->model, worldTransform, std::move(faceIds)});

    for (const geo::Instance& inst : def->children) {
        if (applyVisibilityFilter) {
            const events::EntityRef ref{geo::EntityKind::Instance, inst.id};
            if (geometry.isHidden(ref) || !tags.isEntityVisible(ref)) {
                continue;  // whole subtree skipped
            }
        }
        const QString childName =
            sanitizeGroupNamePart(inst.name.empty() ? ("Instance_" + std::to_string(inst.id)) : inst.name);
        const QString childPath = path + QStringLiteral("/") + childName;
        collectGroups(scene, inst.definitionId, childPath, worldTransform.composed(inst.transform), geometry, tags,
                      /*applyVisibilityFilter=*/false, out);
    }
}

}  // namespace

QByteArray writeObj(const agent::GeometryApi& geometry, const agent::TagStore& tags) {
    std::vector<ObjGroup> groups;
    collectGroups(geometry.scene(), geo::kRootDefinitionId, QStringLiteral("root"), geo::Transform::identity(),
                  geometry, tags, /*applyVisibilityFilter=*/true, groups);

    QStringList lines;
    int nextVertexIndex = 1;  // 1-based, GLOBAL across the whole file -- never reset per group
    int nextNormalIndex = 1;

    for (const ObjGroup& group : groups) {
        if (group.faceIds.empty()) {
            continue;  // no includable face -- omit this group's `g` line entirely
        }

        lines << (QStringLiteral("g ") + group.path);

        // Triangulate every face up front (empty vector = unknown/degenerate,
        // silently skipped) -- triangleLists stays parallel to group.faceIds.
        std::vector<std::vector<geo::Id>> triangleLists;
        triangleLists.reserve(group.faceIds.size());
        std::set<geo::Id> referencedVertexIds;  // ordered -- ascending vertex id
        for (geo::Id faceId : group.faceIds) {
            std::vector<geo::Id> tris = geo::triangulate(*group.model, faceId);
            for (geo::Id vId : tris) {
                referencedVertexIds.insert(vId);
            }
            triangleLists.push_back(std::move(tris));
        }

        std::unordered_map<geo::Id, int> localVertexIndex;
        localVertexIndex.reserve(referencedVertexIds.size());
        for (geo::Id vId : referencedVertexIds) {
            const geo::Vertex* v = group.model->vertex(vId);
            if (v == nullptr) {
                continue;  // defensive -- every id in referencedVertexIds came from a live triangulate() result
            }
            const geo::Vec3 world = group.worldTransform.apply(v->pos);
            lines << vecToObjLine("v", toObjSpace(world));
            localVertexIndex[vId] = nextVertexIndex++;
        }

        // `vn` records: one per included, non-degenerate face (face id
        // ascending, matching group.faceIds' own order) -- a flat normal
        // shared by every triangle that face triangulates into.
        std::vector<int> faceNormalIndex(group.faceIds.size(), 0);  // 0 = no normal (degenerate face)
        for (std::size_t i = 0; i < group.faceIds.size(); ++i) {
            if (triangleLists[i].empty()) {
                continue;
            }
            const geo::Face* face = group.model->face(group.faceIds[i]);
            if (face == nullptr) {
                continue;  // defensive
            }
            const geo::Vec3 worldNormal = geo::normalized(group.worldTransform.applyVector(face->normal));
            lines << vecToObjLine("vn", toObjSpace(worldNormal));
            faceNormalIndex[i] = nextNormalIndex++;
        }

        // `f` records: every triangle geo::triangulate returned for each
        // included face, in that same face-id-ascending order, each corner
        // as `v//vn` (no texture index -- MVP cut, no vt emitted).
        for (std::size_t i = 0; i < group.faceIds.size(); ++i) {
            const std::vector<geo::Id>& tris = triangleLists[i];
            if (tris.empty()) {
                continue;
            }
            const int n = faceNormalIndex[i];
            for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
                const int a = localVertexIndex.at(tris[t]);
                const int b = localVertexIndex.at(tris[t + 1]);
                const int c = localVertexIndex.at(tris[t + 2]);
                QString line = QStringLiteral("f ");
                line += QString::number(a) + QStringLiteral("//") + QString::number(n) + QStringLiteral(" ");
                line += QString::number(b) + QStringLiteral("//") + QString::number(n) + QStringLiteral(" ");
                line += QString::number(c) + QStringLiteral("//") + QString::number(n);
                lines << line;
            }
        }
    }

    return lines.join(QChar('\n')).toUtf8() + "\n";
}

}  // namespace plnr::io
