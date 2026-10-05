#pragma once

#include <vector>

#include <QDialog>
#include <QString>

#include <geo/vec3.h>

class QDoubleSpinBox;
class QLineEdit;

namespace plnr::ui {

// 3D Text dialog: type text plus a height and extrusion depth; on accept exposes the
// extruded-glyph outlines via outlines()/extrusion() for MainWindow to send as
// events::Add3dTextRequested. The only place in the app that touches Qt's font/text machinery;
// the kernel stays Qt-free.
class Text3dDialog : public QDialog {
    Q_OBJECT

public:
    explicit Text3dDialog(QWidget* parent = nullptr);

    // Valid at any time, not just after accept.
    QString text() const;
    double extrusion() const;

    // polygonizeText(text(), height spin's current value) -- what MainWindow sends as
    // Add3dTextRequested::outlines.
    std::vector<std::vector<geo::Vec3>> outlines() const;

    // Polygonizes text at height into closed, z = 0 glyph-boundary loops, flipping painter-space Y
    // (down) to model-space Y (up). MVP cuts: HOLE subpaths (e.g. inside 'O'/'A') are dropped
    // (geo::Model has no multi-loop faces yet); overlapping glyph outlines are unhandled (wrong, no crash).
    static std::vector<std::vector<geo::Vec3>> polygonizeText(const QString& text, double height);

private:
    QLineEdit* textEdit_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QDoubleSpinBox* extrusionSpin_ = nullptr;
};

}  // namespace plnr::ui
