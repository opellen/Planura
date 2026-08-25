#include "text_tool.h"

#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

#include <geo/measure.h>
#include <geo/model.h>

namespace plnr::tools {

namespace {

// Same 2-decimal, no-unit-suffix formatting every tool uses for a plain
// numeric readout -- this app has no unit-system concept anywhere else, so
// area/length/coordinate defaults stay unitless too.
std::string formatNumber(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

std::string formatPoint(const geo::Vec3& p) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << "(" << p.x << ", " << p.y << ", " << p.z << ")";
    return oss.str();
}

// UNVERIFIED -- no confirmed real-the reference modeler Text-tool hint string exists;
// wording is a reasonable placeholder.
constexpr const char* kHint =
    "Click an entity for leader text, or empty space for screen text. Double-click a face for its area.";

}  // namespace

std::string TextTool::defaultTextFor(const geo::Model& model, const geo::PickResult& hit) {
    switch (hit.kind) {
        case geo::PickKind::Vertex: {
            const geo::Vertex* v = model.vertex(hit.id);
            return v ? formatPoint(v->pos) : std::string();
        }
        case geo::PickKind::Edge:
            return formatNumber(geo::edgeLength(model, hit.id));
        case geo::PickKind::Face:
            return formatNumber(geo::faceArea(model, hit.id));
        case geo::PickKind::None:
            break;
    }
    return {};
}

void TextTool::onActivate(ToolContext& ctx) {
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
    ctx.setHint(kHint);
}

void TextTool::onDeactivate(ToolContext& ctx) {
    ctx.setPreview({}, std::nullopt);
    ctx.setInferenceCue(std::nullopt);
}

void TextTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    // Simple hover feedback: a plain marker at whatever pick() currently
    // resolves to, no inference ladder needed since the committed anchor is
    // always the raw pick hit point, not an inferred one.
    const geo::PickResult hit = ctx.pick(e, e.tols);
    ctx.setPreview({}, hit.kind != geo::PickKind::None ? std::optional<geo::Vec3>(hit.point) : std::nullopt);
    ctx.setHint(kHint);
}

void TextTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    const geo::Model* model = ctx.model();

    {
        const geo::PickResult faceHit = ctx.pick(e, geo::PickOptions{0.0, 0.0});
        if (faceHit.kind == geo::PickKind::Face && model) {
            ctx.requestAddLeaderText(faceHit.point, events::EntityRef{geo::EntityKind::Face, faceHit.id},
                                      formatNumber(geo::faceArea(*model, faceHit.id)));
            return;
        }
    }

    const geo::PickResult hit = ctx.pick(e, e.tols);
    if (hit.kind != geo::PickKind::None && model) {
        const std::string defaultText = defaultTextFor(*model, hit);
        const std::optional<std::string> entered = ctx.promptText("Text", defaultText);
        if (!entered) return;  // cancelled -- no annotation added

        const geo::EntityKind kind = hit.kind == geo::PickKind::Vertex ? geo::EntityKind::Vertex : geo::EntityKind::Edge;
        ctx.requestAddLeaderText(hit.point, events::EntityRef{kind, hit.id}, *entered);
        return;
    }

    // Empty space: screen text fixed at this exact widget-pixel position.
    const std::optional<std::string> entered = ctx.promptText("Text", std::string());
    if (!entered) return;  // cancelled
    ctx.requestAddScreenText(e.screen.x(), e.screen.y(), *entered);
}

}  // namespace plnr::tools
