#pragma once

#include <QByteArray>
#include <QString>

#include "agent/annotation_store.h"
#include "agent/asset_repository.h"
#include "agent/axes_store.h"
#include "agent/fog_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/shadow_store.h"
#include "agent/style_store.h"
#include "agent/tag_store.h"
#include "io/plr_writer.h"

// The .plr reader: consumes exactly what plr_writer.h's writeDocument()
// produces (round-trip is the acceptance bar). See the .cpp's top comment for the algorithm.
namespace plnr::io {

// ok==true: applied to every agent argument. ok==false: nothing applied;
// error names the first problem with location context, e.g.
// "definitions[2].mesh.faces[0]: loop references unknown vertex 17".
struct ReadResult {
    bool ok{};
    QString error;
};

// Parses bytes as a .plr document and, on success, replaces every agent
// argument's contents (plus *cameraOut/*metaOut). All-or-nothing: a failed
// load leaves everything untouched, validated in a local staging area first.
ReadResult readDocument(const QByteArray& bytes, agent::GeometryApi& geometry, agent::TagStore& tags,
                         agent::GuideStore& guides, agent::AnnotationStore& annotations,
                         agent::SectionStore& sections, agent::AxesStore& axes, agent::MaterialRepository& materials,
                         agent::AssetRepository& assets, agent::StyleStore& style, agent::ShadowStore& shadows,
                         agent::FogStore& fog, CameraState* cameraOut, DocumentMeta* metaOut);

}  // namespace plnr::io
