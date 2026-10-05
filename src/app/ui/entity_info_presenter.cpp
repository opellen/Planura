#include "entity_info_presenter.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <geo/measure.h>
#include <geo/model.h>
#include <geo/scene.h>
#include <geo/solid.h>

#include "agent/geometry_api.h"
#include "agent/material_repository.h"
#include "agent/selection_store.h"

namespace plnr::ui {

namespace {

// Resolved material name for the Entity Info line ("Default" when unpainted or unnamed).
// Front-slot raw lookup only, NOT instance-inheritance-aware: a Face nested in a painted
// Instance shows "Default" unless it carries its own override.
QString materialNameFor(const agent::MaterialRepository* materials, const events::EntityRef& ref) {
    if (!materials) return QStringLiteral("Default");
    const agent::MaterialAssignment* assignment = materials->assignment(ref);
    const geo::Id frontId = assignment ? assignment->frontMaterialId : geo::Id{0};
    if (frontId == 0) return QStringLiteral("Default");
    const agent::Material* material = materials->material(frontId);
    return material ? QString::fromStdString(material->name) : QStringLiteral("Default");
}

// Determinant of transform's 3x3 linear part: the scale factor on a Definition's solidVolume
// for an Instance's world-space volume (translation doesn't affect volume). geo::Transform
// has no determinant accessor.
double transformDeterminant(const geo::Transform& t) {
    return geo::dot(t.col0, geo::cross(t.col1, t.col2));
}

}  // namespace

EntityInfoPresenter::EntityInfoPresenter(QLabel* infoLabel)
    : Presenter(QStringLiteral("EntityInfoPresenter"), infoLabel), infoLabel_(infoLabel) {}

void EntityInfoPresenter::onRegister() {
    subscribe<events::SelectionChanged>(&EntityInfoPresenter::onSelectionChanged);
    subscribe<events::GeometryChanged>(&EntityInfoPresenter::onGeometryChanged);
    subscribe<events::MaterialsChanged>(&EntityInfoPresenter::onMaterialsChanged);
    render();  // covers a selection that existed before this presenter registered
}

void EntityInfoPresenter::onSelectionChanged(const events::SelectionChanged& /*event*/) {
    render();
}

void EntityInfoPresenter::onGeometryChanged(const events::GeometryChanged& /*event*/) {
    render();
}

void EntityInfoPresenter::onMaterialsChanged(const events::MaterialsChanged& /*event*/) {
    render();
}

void EntityInfoPresenter::render() {
    auto selectionStore = context().agentAs<agent::SelectionStore>(agent::kSelectionStoreName);
    auto geometryStore = context().agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!selectionStore || !geometryStore) {
        infoLabel_->setText(QStringLiteral("No Selection"));
        return;
    }

    const geo::Model& model = geometryStore->model();
    // May be nullptr (MaterialRepository not registered); materialNameFor() tolerates it.
    auto materialsStore = context().agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);

    // Skip refs the model no longer knows about (stale between a prune and this render);
    // SelectionStore::prune usually removes them first.
    std::vector<events::EntityRef> live;
    live.reserve(selectionStore->items().size());
    for (const events::EntityRef& ref : selectionStore->items()) {
        bool alive = false;
        switch (ref.kind) {
            case geo::EntityKind::Vertex: alive = model.vertex(ref.id) != nullptr; break;
            case geo::EntityKind::Edge: alive = model.edge(ref.id) != nullptr; break;
            case geo::EntityKind::Face: alive = model.face(ref.id) != nullptr; break;
            case geo::EntityKind::Instance:
                // Instances aren't part of `model` -- alive means still a direct
                // child of the root.
                alive = geometryStore->scene().findInstance(geo::kRootDefinitionId, ref.id) != nullptr;
                break;
        }
        if (alive) live.push_back(ref);
    }

    if (live.empty()) {
        infoLabel_->setText(QStringLiteral("No Selection"));
        return;
    }

    if (live.size() == 1) {
        const events::EntityRef& ref = live.front();
        switch (ref.kind) {
            case geo::EntityKind::Vertex: {
                const geo::Vertex* v = model.vertex(ref.id);
                infoLabel_->setText(QStringLiteral("Vertex\nPosition: (%1, %2, %3)")
                                         .arg(v->pos.x, 0, 'f', 2)
                                         .arg(v->pos.y, 0, 'f', 2)
                                         .arg(v->pos.z, 0, 'f', 2));
                return;
            }
            case geo::EntityKind::Edge: {
                const double len = geo::edgeLength(model, ref.id);
                infoLabel_->setText(QStringLiteral("Edge\nLength: %1 m").arg(len, 0, 'f', 2));
                return;
            }
            case geo::EntityKind::Face: {
                const double area = geo::faceArea(model, ref.id);
                infoLabel_->setText(QStringLiteral("Face\nArea: %1 m\u00B2\nMaterial: %2")
                                         .arg(area, 0, 'f', 2)
                                         .arg(materialNameFor(materialsStore.get(), ref)));
                return;
            }
            case geo::EntityKind::Instance: {
                const geo::Scene& scene = geometryStore->scene();
                const geo::Instance* inst = scene.findInstance(geo::kRootDefinitionId, ref.id);
                if (inst == nullptr) break;  // shouldn't happen -- `live` above already filtered dead refs
                const geo::Definition* def = scene.definition(inst->definitionId);
                const QString kindWord =
                    (def != nullptr && def->isGroup) ? QStringLiteral("Group") : QStringLiteral("Component");

                // Solid classification is geo::solid's job (isSolidDefinition); this presenter only
                // formats. No caching: render() runs only on Selection/Geometry/MaterialsChanged.
                const bool solid = (def != nullptr) && geo::isSolidDefinition(*def);
                const QString kindLabel = solid ? (QStringLiteral("Solid ") + kindWord) : kindWord;

                // N = live instances anywhere in the model referencing this Definition; counting
                // kRootDefinitionId's children is complete (no nested-instance creation path yet).
                std::size_t instanceCount = 0;
                for (const geo::Instance& child : scene.root().children) {
                    if (child.definitionId == inst->definitionId) ++instanceCount;
                }

                QString text = QStringLiteral("%1 (%2 in model)").arg(kindLabel).arg(instanceCount);
                // Name is separate from this kind/count header; append the quoted name only when
                // there is one (distinguishes unnamed from named "").
                if (!inst->name.empty()) {
                    text += QStringLiteral(" \"%1\"").arg(QString::fromStdString(inst->name));
                }
                if (solid) {
                    // World-space volume = |det(instance transform)| x Definition's solidVolume;
                    // abs() because a mirrored transform's negative det still encloses positive volume.
                    const double worldVolume =
                        std::abs(transformDeterminant(inst->transform)) * geo::solidVolume(def->model);
                    text += QStringLiteral("\nVolume: %1 m\u00B3").arg(worldVolume, 0, 'f', 2);
                }

                const std::size_t edgeCount = def != nullptr ? def->model.edges().size() : 0;
                const std::size_t faceCount = def != nullptr ? def->model.faces().size() : 0;
                text += QStringLiteral("\n%1 %2, %3 %4\nMaterial: %5")
                            .arg(edgeCount)
                            .arg(edgeCount == 1 ? QStringLiteral("edge") : QStringLiteral("edges"))
                            .arg(faceCount)
                            .arg(faceCount == 1 ? QStringLiteral("face") : QStringLiteral("faces"))
                            .arg(materialNameFor(materialsStore.get(), ref));

                infoLabel_->setText(text);
                return;
            }
        }
    }

    std::size_t vertices = 0;
    std::size_t edges = 0;
    std::size_t faces = 0;
    std::size_t instances = 0;
    for (const events::EntityRef& ref : live) {
        switch (ref.kind) {
            case geo::EntityKind::Vertex: ++vertices; break;
            case geo::EntityKind::Edge: ++edges; break;
            case geo::EntityKind::Face: ++faces; break;
            case geo::EntityKind::Instance: ++instances; break;
        }
    }
    infoLabel_->setText(QStringLiteral("%1 Entities (%2 vertices, %3 edges, %4 faces, %5 instances)")
                             .arg(live.size())
                             .arg(vertices)
                             .arg(edges)
                             .arg(faces)
                             .arg(instances));
}

}  // namespace plnr::ui
