#include "ui/theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

#include "constants/design_tokens.h"

namespace plnr::ui::theme {

void apply(QApplication& app) {
    // Fusion, not the platform style: the Windows native styles resolve a
    // number of controls (menus, combo popups, spin buttons) straight from
    // system theme bitmaps that a stylesheet cannot fully recolor.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    const QColor text = design::color(design::kTextPrimary);
    const QColor textDisabled = design::color(design::kTextDisabled);

    QPalette palette;
    palette.setColor(QPalette::Window, design::color(design::kSurface0));
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, design::color(design::kSurface2));
    palette.setColor(QPalette::AlternateBase, design::color(design::kSurface1));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, textDisabled);
    palette.setColor(QPalette::Button, design::color(design::kSurface1));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, design::color(design::kTextOnAccent));
    palette.setColor(QPalette::Highlight, design::color(design::kSurfacePressed));
    palette.setColor(QPalette::HighlightedText, text);
    palette.setColor(QPalette::ToolTipBase, design::color(design::kSurface2));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Link, design::color(design::kAccentInteractive));
    palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled);
    app.setPalette(palette);

    // Pin only the base size; without it unstyled text follows the OS message
    // font (9pt on Windows). The viewport overlays set their own sizes.
    QFont base = app.font();
    base.setPixelSize(design::kTypeSizePx);
    app.setFont(base);

    app.setStyleSheet(appStyleSheet());
}

QString appStyleSheet() {
    // Every color is a role placeholder (e.g. {surface-0}) resolved by
    // design::resolveRoles(); no literal hex here. Concrete selectors only:
    // no blanket QWidget rule, so the QOpenGLWidget viewport stays untouched.
    const QString kTemplate = QStringLiteral(R"qss(
QMainWindow {
    background: {surface-0};
}

/* ---- menus ------------------------------------------------------------ */
QMenuBar {
    background: {surface-1};
    color: {text-primary};
}
QMenuBar::item {
    padding: 4px 10px;
    background: transparent;
}
QMenuBar::item:selected {
    background: {surface-hover};
}
QMenuBar::item:pressed {
    background: {surface-pressed};
}
QMenu {
    background: {surface-2};
    color: {text-primary};
    border: 1px solid {outline-strong};
    border-radius: 4px;
    padding: 4px 0;
}
QMenu::item {
    padding: 5px 24px 5px 16px;
    background: transparent;
}
QMenu::item:selected {
    background: {surface-hover};
    color: {text-primary};
}
QMenu::item:disabled {
    color: {text-disabled};
    background: transparent;
}
QMenu::separator {
    height: 1px;
    background: {outline};
    margin: 4px 8px;
}

/* ---- toolbars --------------------------------------------------------- */
QToolBar {
    background: {surface-1};
    border: none;
    spacing: 2px;
}
QToolBar QToolButton {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    border-radius: 0;
    padding: 3px;
}
QToolBar QToolButton:hover {
    background: {surface-hover};
}
QToolBar QToolButton:checked {
    background: {surface-hover};
    border-bottom: 2px solid {accent-interactive};
}
QToolBar QToolButton:pressed {
    background: {surface-pressed};
}
QToolBar::separator {
    background: {outline-strong};
    width: 1px;
    height: 1px;
    margin: 4px;
}
/* Context options bar: tool face, Sides spinner, units, projection toggles. */
QToolBar#contextBar {
    background: {surface-1};
    padding: 0 6px;
}
QToolBar#contextBar QLabel {
    color: {text-primary};
}
QToolBar#contextBar QLabel#contextBarUnits {
    color: {text-secondary};
    padding: 0 8px;
}
/* 18 rail buttons (3 modes + 15 tool slots) must fit the default 900px
   window: zero toolbar spacing and a tighter padding than the horizontal
   toolbars keep the column under the viewport height. */
QToolBar#activityRail {
    spacing: 0;
}
QToolBar#activityRail QToolButton {
    border-bottom: none;
    border-left: 2px solid transparent;
    padding: 2px 5px;
}
QToolBar#activityRail::separator {
    margin: 2px 4px;
}
QToolBar#activityRail QToolButton:checked {
    background: {surface-hover};
    border-bottom: none;
    border-left: 2px solid {accent-interactive};
}

/* Flyout palette of a rail family slot: a floating surface of icon+label rows. */
QFrame#toolFlyoutPopup {
    background: {surface-2};
    border: 1px solid {outline-strong};
}
QFrame#toolFlyoutPopup QToolButton {
    background: transparent;
    color: {text-primary};
    border: none;
    border-left: 2px solid transparent;
    border-radius: 0;
    padding: 4px 12px 4px 6px;
    text-align: left;
}
QFrame#toolFlyoutPopup QToolButton:hover {
    background: {surface-hover};
}
QFrame#toolFlyoutPopup QToolButton:checked {
    background: {surface-hover};
    border-left: 2px solid {accent-interactive};
}
QFrame#toolFlyoutPopup QToolButton:pressed {
    background: {surface-pressed};
}

/* ---- tabs (editor tab bar is a future hook) --------------------------- */
QTabBar#editorTabBar {
    background: {surface-0};
}
QTabBar#editorTabBar::tab {
    background: {surface-1};
    color: {text-secondary};
    padding: 5px 12px;
    border: none;
    border-top: 2px solid transparent;
}
QTabBar#editorTabBar::tab:hover {
    background: {surface-hover};
    color: {text-primary};
}
QTabBar#editorTabBar::tab:selected {
    background: {surface-0};
    color: {text-primary};
    border-top: 2px solid {accent-interactive};
}

/* Focus ring of the focused editor group; the group reserves a 1px margin for it. */
QWidget#editorGroup[focused="true"] {
    border: 1px solid {outline-focus};
}

/* ---- tray containers -------------------------------------------------- */
QScrollArea {
    background: transparent;
    border: none;
}
QScrollArea > QWidget > QWidget {
    background: transparent;
}

/* ---- splitters (future hook) ------------------------------------------ */
QSplitter::handle {
    background: {surface-0};
}
QSplitter::handle:horizontal {
    width: 3px;
}
QSplitter::handle:vertical {
    height: 3px;
}
QSplitter::handle:hover {
    background: {accent-interactive};
}

/* ---- item views ------------------------------------------------------- */
QListWidget, QListView, QTreeView {
    background: {surface-2};
    color: {text-primary};
    border: 1px solid {outline};
    outline: none;
}
QListWidget::item, QListView::item {
    padding: 3px 4px;
    border-left: 2px solid transparent;
}
QListWidget::item:hover, QListView::item:hover, QTreeView::item:hover {
    background: {surface-hover};
}
QListWidget::item:selected, QListView::item:selected {
    background: {surface-hover};
    color: {text-primary};
    border-left: 2px solid {accent-interactive};
}
QTreeView::item:selected {
    background: {surface-hover};
    color: {text-primary};
}

/* ---- inputs ----------------------------------------------------------- */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: {surface-2};
    color: {text-primary};
    border: 1px solid {outline};
    border-radius: 4px;
    padding: 3px 6px;
    selection-background-color: {surface-pressed};
    selection-color: {text-primary};
}
QLineEdit:hover, QComboBox:hover, QSpinBox:hover, QDoubleSpinBox:hover {
    border: 1px solid {outline-hover};
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
    border: 1px solid {outline-focus};
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    background: {surface-1};
    color: {text-disabled};
    border: 1px solid {outline};
}
QComboBox QAbstractItemView {
    background: {surface-2};
    color: {text-primary};
    border: 1px solid {outline-strong};
    outline: none;
    selection-background-color: {surface-hover};
    selection-color: {text-primary};
}

/* ---- buttons (Secondary look) ----------------------------------------- */
QPushButton {
    background: transparent;
    color: {text-secondary};
    border: 1px solid {outline};
    border-radius: 4px;
    padding: 4px 12px;
}
QPushButton:hover:enabled {
    background: {surface-hover};
    color: {text-primary};
}
QPushButton:pressed:enabled {
    background: {surface-pressed};
}
QPushButton:disabled {
    background: transparent;
    color: {text-disabled};
    border: 1px solid {outline};
}
QPushButton:focus {
    border: 1px solid {outline-focus};
}

/* ---- checkboxes ------------------------------------------------------- */
QCheckBox {
    color: {text-primary};
}
QCheckBox:disabled {
    color: {text-disabled};
}
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    background: {surface-2};
    border: 1px solid {outline-strong};
    border-radius: 3px;
}
QCheckBox::indicator:hover {
    border: 1px solid {outline-hover};
}
QCheckBox::indicator:checked {
    background: {accent-interactive};
    border: 1px solid {accent-interactive};
}

/* ---- scrollbars (thin, no arrow buttons) ------------------------------ */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 0;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 0;
}
QScrollBar::handle {
    background: {outline};
    border-radius: 5px;
    min-height: 24px;
    min-width: 24px;
}
QScrollBar::handle:hover {
    background: {outline-hover};
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
}
QScrollBar::add-page, QScrollBar::sub-page {
    background: transparent;
}

/* ---- status bar / tooltips / dialogs ---------------------------------- */
QStatusBar {
    background: {surface-1};
    color: {text-secondary};
}
QStatusBar QLabel {
    color: {text-secondary};
}
QToolTip {
    background: {surface-2};
    color: {text-primary};
    border: 1px solid {outline-strong};
    padding: 3px 6px;
}
QDialog, QMessageBox {
    background: {surface-0};
}
)qss");

    return design::resolveRoles(kTemplate);
}

}  // namespace plnr::ui::theme
