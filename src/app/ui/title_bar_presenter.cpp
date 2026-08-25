#include "ui/title_bar_presenter.h"

#include <string>

#include <QFileInfo>
#include <QMainWindow>
#include <QString>

#include "app_version.h"
#include "agent/document_store.h"

namespace plnr::ui {

TitleBarPresenter::TitleBarPresenter(ordo::core::AppKernel& kernel, QMainWindow* window)
    : Presenter(kernel, QStringLiteral("TitleBarPresenter"), window), window_(window) {}

void TitleBarPresenter::onRegister() {
    subscribe<events::DocumentStateChanged>(&TitleBarPresenter::onDocumentStateChanged);
    refresh();
}

void TitleBarPresenter::onDocumentStateChanged(const events::DocumentStateChanged& /*event*/) {
    refresh();
}

void TitleBarPresenter::refresh() {
    auto document = kernel().agentAs<agent::DocumentStore>(agent::kDocumentStoreName);
    if (!document || !window_) return;  // No DocumentStore registered -- leave whatever title was already set.

    const std::string& path = document->filePath();
    const QString displayName =
        path.empty() ? QStringLiteral("Untitled") : QFileInfo(QString::fromStdString(path)).fileName();

    // The " v<A.B.C>" suffix identifies the running build -- see
    // app_version.h for the bump discipline and the ODR trap that forbids a
    // second version header.
    window_->setWindowTitle(displayName + QStringLiteral("[*] - Planura v") +
                            QString::fromUtf8(plnr::kAppVersion.data(),
                                               static_cast<qsizetype>(plnr::kAppVersion.size())));
    window_->setWindowModified(document->dirty());
    window_->setWindowFilePath(path.empty() ? QString() : QString::fromStdString(path));
}

}  // namespace plnr::ui
