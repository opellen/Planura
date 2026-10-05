// Ported from ordo-state-designer model/io_events.h.
#pragma once

#include <QString>
#include <any>
#include <string_view>

namespace plnr::infra::events {

enum class IoOperationKind {
    OpenDocument,
    SaveDocument,
    ImportObj,
    ExportObj,
    LoadTexture
};

struct IoStarted {
    static constexpr std::string_view eventName = "IoStarted";
    quint64 opId = 0;
    IoOperationKind kind = IoOperationKind::OpenDocument;
    QString targetPath;
    QString title;
    bool isModal = false;
};

struct IoProgress {
    static constexpr std::string_view eventName = "IoProgress";
    quint64 opId = 0;
    int percentage = -1;  // -1 indicates indeterminate progress
    QString statusMessage;
};

struct IoCompleted {
    static constexpr std::string_view eventName = "IoCompleted";
    quint64 opId = 0;
    IoOperationKind kind = IoOperationKind::OpenDocument;
    QString targetPath;
    std::any resultPayload;
};

struct IoFailed {
    static constexpr std::string_view eventName = "IoFailed";
    quint64 opId = 0;
    IoOperationKind kind = IoOperationKind::OpenDocument;
    QString targetPath;
    QString errorMessage;
};

}  // namespace plnr::infra::events
