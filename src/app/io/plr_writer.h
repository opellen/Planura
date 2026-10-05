#pragma once

#include <QJsonDocument>
#include <QString>

#include <geo/vec3.h>

#include "agent/annotation_store.h"
#include "agent/axes_store.h"
#include "agent/events.h"
#include "agent/fog_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/shadow_store.h"
#include "agent/style_store.h"
#include "agent/tag_store.h"

// The .plr writer: walks live agent state into the .plr JSON schema.
namespace plnr::io {

// Caller-supplied document metadata ("meta" block). The writer never reads
// clocks/app state itself -- appVersion/savedAt are entirely the caller's
// job. units defaults to "in" (this app's internal unit).
struct DocumentMeta {
    QString appVersion;
    QString savedAt;
    QString units{"in"};
};

// Camera state as of save time -- a plain caller-supplied snapshot, shaped
// to match the eventual CameraStore 1:1 (not built yet).
struct CameraState {
    geo::Vec3 target;
    double azimuthDeg{};
    double elevationDeg{};
    double distance{};
    double fovYDeg{};
    // Reuses events::Projection directly (cheap to share across layers).
    // COMPAT: absent on read (pre-projection .plr) defaults to Perspective --
    // the one optional field in an otherwise-required camera block.
    events::Projection projection{events::Projection::Perspective};
};

// Serializes the live document to the .plr JSON schema.
// Deterministic: identical agent state -> byte-identical output. Read-only.
QJsonDocument writeDocument(const agent::GeometryApi& geometry, const agent::TagStore& tags,
                             const agent::GuideStore& guides, const agent::AnnotationStore& annotations,
                             const agent::SectionStore& sections, const agent::AxesStore& axes,
                             const agent::MaterialRepository& materials, const agent::StyleStore& style,
                             const agent::ShadowStore& shadows, const agent::FogStore& fog, const CameraState& camera,
                             const DocumentMeta& meta);

}  // namespace plnr::io
