#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <geo/journal.h>
#include <geo/model.h>
#include <geo/scene.h>

#include <ordo/core/kernel.h>

#include "agent/annotation_store.h"
#include "agent/axes_store.h"
#include "agent/geometry_api.h"
#include "agent/guide_store.h"
#include "agent/material_repository.h"
#include "agent/section_store.h"
#include "agent/tag_store.h"
#include "agent/transaction_delta.h"
#include "agent/undo_store.h"

// One transaction's live capture (one user gesture = one mutating Intent dispatch). NOT a registered Agent:
// a stack-local helper for a single Command::execute(). No-op if GeometryApi/UndoStore is absent.
namespace plnr::agent {

// Applies delta (mesh/scene ops + aux diffs) to the live agents in the given direction. Shared by
// Transaction::abort() (Backward) and UndoCommand/RedoCommand. geometry required; aux pointers may be nullptr.
// Journals MUST already be detached on both sides.
enum class ApplyDirection { Backward, Forward };

void applyTransactionDelta(GeometryApi& geometry, TagStore* tags, GuideStore* guides, AnnotationStore* annotations,
                            SectionStore* sections, AxesStore* axes, MaterialRepository* materials,
                            const TransactionDelta& delta, ApplyDirection direction);

class Transaction {
public:
    explicit Transaction(ordo::core::CommandContext& context);

    // Defensive-only: every real caller pairs begin() with commit()/abort(). If neither ran,
    // detaching here (no rollback) prevents dangling journal pointers still registered in Model/Scene.
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    // Begins capturing: copies the aux agents' current state as before-images, attaches a
    // SceneJournal to the scene and a ModelJournal to root (a definition created mid-transaction
    // gets its own). No-op if GeometryApi/UndoStore is absent.
    void begin();

    // Ends the transaction: for each touched aux category, compares an after-copy against begin()'s
    // before-copy, including it only if it differs. Pushes the delta onto UndoStore (clearing
    // redo) if anything changed; otherwise discards it. No-op if begin() never activated.
    void commit();

    // Ends the transaction WITHOUT committing: detaches every journal first (so backward replay
    // can't re-record itself), then applies the captured delta backward, restoring live state to
    // exactly what it was at begin(). Nothing pushed onto UndoStore; no-op if begin() never activated.
    void abort();

    // Aux category (Transaction-only). No Styles entry: style is view-setting state, never captured into an aux-diff.
    enum class AuxStore { Tags, Guides, Annotations, Sections, Axes, Materials, Hidden };

    // Marks one aux category touched; no-op if no transaction is active. notifyMutation() can only call markAllAuxTouched().
    void markStoreTouched(AuxStore which);

    // Marks every aux category touched at once (see UndoStore::notifyMutation()).
    void markAllAuxTouched();

    bool isActive() const { return isActive_; }

private:
    class CapturingModelJournal : public geo::ModelJournal {
    public:
        CapturingModelJournal(Transaction& owner, geo::Id definitionId) : owner_(owner), definitionId_(definitionId) {}

        void vertexCreated(geo::Id id, geo::Vec3 pos) override;
        void vertexMoved(geo::Id id, geo::Vec3 before, geo::Vec3 after) override;
        void vertexDeleted(geo::Id id, geo::Vec3 posBefore) override;
        void edgeCreated(geo::Id id, geo::Id v0, geo::Id v1) override;
        void edgeDeleted(geo::Id id, geo::Id v0, geo::Id v1) override;
        void faceCreated(geo::Id id, const std::vector<geo::Id>& loop) override;
        void faceDeleted(geo::Id id, const std::vector<geo::Id>& loopBefore) override;
        void faceLoopModified(geo::Id id, const std::vector<geo::Id>& loopBefore,
                               const std::vector<geo::Id>& loopAfter) override;

    private:
        Transaction& owner_;
        geo::Id definitionId_;
    };

    class CapturingSceneJournal : public geo::SceneJournal {
    public:
        explicit CapturingSceneJournal(Transaction& owner) : owner_(owner) {}

        void definitionCreated(geo::Id id, const std::string& name, bool isGroup) override;
        void definitionDeleted(geo::Id id, const std::string& name, bool isGroup) override;
        void instanceCreated(geo::Id parentDefId, geo::Id id, geo::Id definitionId, const geo::Transform& transform,
                              const std::string& name) override;
        void instanceDeleted(geo::Id parentDefId, geo::Id id, geo::Id definitionId, const geo::Transform& transformBefore,
                              const std::string& nameBefore) override;
        void instanceModified(geo::Id parentDefId, geo::Id id, const geo::Transform& before, const geo::Transform& after,
                               const std::string& nameBefore, const std::string& nameAfter) override;

    private:
        Transaction& owner_;
    };

    friend class CapturingModelJournal;
    friend class CapturingSceneJournal;

    void pushOp(TransactionOp op);
    // Attaches a CapturingModelJournal to the just-created definition's Model so its mesh events are captured too.
    void attachDefinitionJournal(geo::Id definitionId);
    // Detaches every attached journal and clears the per-definition list; leaves ops_/before-images/touched flags
    // (commit()/abort() consume those). Always safe.
    void detachJournals();

    std::shared_ptr<GeometryApi> geometry_;
    std::shared_ptr<TagStore> tags_;
    std::shared_ptr<GuideStore> guides_;
    std::shared_ptr<AnnotationStore> annotations_;
    std::shared_ptr<SectionStore> sections_;
    std::shared_ptr<AxesStore> axes_;
    std::shared_ptr<MaterialRepository> materials_;
    std::shared_ptr<UndoStore> undo_;

    bool isActive_ = false;
    geo::Scene* scene_ = nullptr;  // valid only while isActive_

    std::vector<TransactionOp> ops_;
    std::vector<std::pair<geo::Id, std::unique_ptr<CapturingModelJournal>>> definitionJournals_;
    CapturingModelJournal rootModelJournal_;
    CapturingSceneJournal sceneJournal_;

    bool tagsTouched_ = false;
    bool guidesTouched_ = false;
    bool annotationsTouched_ = false;
    bool sectionsTouched_ = false;
    bool axesTouched_ = false;
    bool materialsTouched_ = false;
    bool hiddenTouched_ = false;

    TagsSnapshot tagsBefore_;
    std::vector<Guide> guidesBefore_;
    AnnotationsSnapshot annotationsBefore_;
    std::vector<SectionPlane> sectionsBefore_;
    Frame axesBefore_;
    MaterialsSnapshot materialsBefore_;
    std::unordered_set<events::EntityRef> hiddenBefore_;
};

// Wraps RealCommand so dispatching EventT captures an undo step: begin(), RealCommand::execute(), commit().
// Mesh/scene changes need no extra ping (journals). Undo/Redo are NEVER wrapped.
template <typename RealCommand, typename EventT>
class UndoCaptureCommand : public ordo::core::Command<EventT> {
public:
    void execute(const EventT& event, ordo::core::CommandContext& context) override {
        Transaction txn(context);
        txn.begin();

        RealCommand{}.execute(event, context);

        txn.commit();
    }
};

}  // namespace plnr::agent
