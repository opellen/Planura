#include "solid_tool.h"

#include <Qt>

namespace plnr::tools {

namespace {

// UNVERIFIED wording except Subtract's two strings, which are verbatim;
// every other op's text is a placeholder in the same voice.
std::string firstStageHint(events::SolidOp op) {
    switch (op) {
        case events::SolidOp::Union: return "Select the first solid group or component to union.";
        case events::SolidOp::OuterShell: return "Select the first solid group or component.";
        case events::SolidOp::Subtract: return "Select the cutting solid group or component.";
        case events::SolidOp::Trim: return "Select the trimming solid group or component.";
        case events::SolidOp::Intersect: return "Select the first solid group or component to intersect.";
        case events::SolidOp::Split: return "Select the first solid group or component to split.";
    }
    return "Select a solid group or component.";  // unreachable -- SolidOp is exhaustively handled above
}

std::string secondStageHint(events::SolidOp op) {
    switch (op) {
        case events::SolidOp::Union: return "Select the next solid to union.";
        case events::SolidOp::OuterShell: return "Select the next solid to add to the outer shell.";
        case events::SolidOp::Subtract: return "Select the solid to cut.";
        case events::SolidOp::Trim: return "Select the solid to trim.";
        case events::SolidOp::Intersect: return "Select the next solid to intersect.";
        case events::SolidOp::Split: return "Select the second solid to split.";
    }
    return "Select the next solid.";  // unreachable -- SolidOp is exhaustively handled above
}

// What happens after a completing second click -- see the class comment.
enum class SecondClickBehavior { FoldContinue, ResetStayActive, DeactivateOnApply };

SecondClickBehavior behaviorFor(events::SolidOp op) {
    switch (op) {
        case events::SolidOp::Union:
        case events::SolidOp::OuterShell:
        case events::SolidOp::Intersect:
            return SecondClickBehavior::FoldContinue;
        case events::SolidOp::Subtract:
        case events::SolidOp::Trim:
            return SecondClickBehavior::ResetStayActive;
        case events::SolidOp::Split:
            return SecondClickBehavior::DeactivateOnApply;
    }
    return SecondClickBehavior::ResetStayActive;  // unreachable -- SolidOp is exhaustively handled above
}

}  // namespace

SolidTool::SolidTool(events::SolidOp op) : op_(op) {}

void SolidTool::onActivate(ToolContext& ctx) {
    solidCache_.clear();
    firstOperand_ = geo::kInvalidId;
    basePrompt_ = firstStageHint(op_);
    ctx.setHint(basePrompt_);
}

void SolidTool::onDeactivate(ToolContext& ctx) {
    // No preview overlay is ever set by this tool, but every Tool MUST clear
    // it on deactivate per tool.h's contract -- defensive no-op.
    ctx.setPreview({}, std::nullopt);
    solidCache_.clear();
    firstOperand_ = geo::kInvalidId;
}

void SolidTool::onKeyDown(ToolContext& ctx, int key, bool ctrl) {
    (void)ctrl;
    if (key != Qt::Key_Escape) return;
    firstOperand_ = geo::kInvalidId;
    basePrompt_ = firstStageHint(op_);
    ctx.setHint(basePrompt_);
}

SolidTool::Classification SolidTool::classify(ToolContext& ctx, const geo::ScenePickResult& hit) {
    if (hit.instanceId == geo::kInvalidId) return {};  // loose root geometry, or nothing hit at all
    const ToolContext::SolidTargetInfo info = ctx.solidTargetInfo(hit.instanceId);
    if (!info.valid) return {};
    return {cachedIsDefinitionSolid(ctx, info.definitionId), info.isGroup};
}

bool SolidTool::cachedIsDefinitionSolid(ToolContext& ctx, geo::Id definitionId) {
    const auto it = solidCache_.find(definitionId);
    if (it != solidCache_.end()) return it->second;
    const bool solid = ctx.isDefinitionSolid(definitionId);
    solidCache_.emplace(definitionId, solid);
    return solid;
}

void SolidTool::onPointerMove(ToolContext& ctx, const PointerEvent& e) {
    const geo::ScenePickResult hit = ctx.pickScene(e, e.tols);

    if (hit.kind == geo::PickKind::None) {
        // Nothing under the cursor: re-assert the current stage's own
        // instruction rather than leaving a stale hover classification.
        ctx.setHint(basePrompt_);
        return;
    }

    const Classification info = classify(ctx, hit);
    if (!info.solid) {
        ctx.setHint("Not a solid");
        return;
    }
    ctx.setHint(info.isGroup ? "Solid Group" : "Solid Component");
}

void SolidTool::onPointerDown(ToolContext& ctx, const PointerEvent& e) {
    // Cache cleared on every click -- mousemove-frequency hover checks are
    // the expensive case the cache targets.
    solidCache_.clear();

    const geo::ScenePickResult hit = ctx.pickScene(e, e.tols);
    const Classification info = classify(ctx, hit);
    if (!info.solid) {
        ctx.setHint("Not a solid");  // ignore -- no state advance (verified non-solid-click behavior)
        return;
    }

    const geo::Id instanceId = hit.instanceId;

    if (firstOperand_ == geo::kInvalidId) {
        // First click of this stage.
        firstOperand_ = instanceId;
        basePrompt_ = secondStageHint(op_);
        ctx.setHint(basePrompt_);
        return;
    }

    if (instanceId == firstOperand_) {
        return;  // clicking the same solid again isn't a valid second operand -- ignored, no state advance
    }

    // Second click: dispatch. Order matters for Subtract/Trim (first =
    // cutter, last = target). before/after bracket the send so a rejected
    // op (no mutation) can be told apart from success (kernel_.send is fire-and-forget).
    const geo::Id before = ctx.lastRootInstanceId();
    ctx.requestSolidOp(op_, {firstOperand_, instanceId});
    const geo::Id after = ctx.lastRootInstanceId();
    const bool applied = after != geo::kInvalidId && after != before;

    switch (behaviorFor(op_)) {
        case SecondClickBehavior::FoldContinue:
            if (applied) {
                // The just-created result becomes the running first operand for a
                // further click (the scene's "always appends" contract).
                firstOperand_ = after;
                basePrompt_ = secondStageHint(op_);
                ctx.setHint(basePrompt_);
            }
            // On rejection nothing was consumed -- leave firstOperand_
            // untouched. Does not touch the hint: SolidOpCommand already
            // reported the rejection reason; overwriting it would hide it.
            break;

        case SecondClickBehavior::ResetStayActive:
            // Subtract/Trim: exactly two clicks, then back to stage 1
            // regardless of outcome; only announce the reset hint on success.
            firstOperand_ = geo::kInvalidId;
            basePrompt_ = firstStageHint(op_);
            if (applied) ctx.setHint(basePrompt_);
            break;

        case SecondClickBehavior::DeactivateOnApply:
            // Split: applies at the 2nd click and deactivates --
            // requestToolChange runs onToolChanged synchronously, which
            // calls this tool's own onDeactivate before returning here.
            if (applied) ctx.requestToolChange(events::ToolId::Select);
            break;
    }
}

}  // namespace plnr::tools
