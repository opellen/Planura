#pragma once

#include <vector>

#include <QDialog>
#include <QString>

#include <geo/vec3.h>

class QDoubleSpinBox;
class QLineEdit;

namespace plnr::ui {

// the reference modeler's 3D Text dialog: lets the user type text plus a height and
// extrusion depth, then on accept exposes the resulting extruded-glyph
// outlines via outlines()/extrusion() for MainWindow to funnel into
// events::Add3dTextRequested. Deliberately the only place in the app that
// touches Qt's font/text machinery -- the kernel/domain layer stays Qt-free.
// MainWindow owns the kernel.send() call; this dialog never reaches it.
class Text3dDialog : public QDialog {
    Q_OBJECT

public:
    explicit Text3dDialog(QWidget* parent = nullptr);

    // Valid at any time (not just after accept -- MainWindow only reads
    // these from its accepted() handler, but nothing here requires that).
    QString text() const;
    double extrusion() const;

    // polygonizeText(text(), height spin's current value) -- what
    // MainWindow actually sends as events::Add3dTextRequested::outlines.
    std::vector<std::vector<geo::Vec3>> outlines() const;

    // Polygonizes text at height into closed, z = 0 glyph-boundary loops,
    // flipping painter-space Y (down) to model-space Y (up). MVP cuts: HOLE
    // subpaths (e.g. inside 'O'/'A') are dropped -- only the dominant winding
    // survives, since geo::Model has no multi-loop-face support yet.
    // Overlapping glyph outlines are also unhandled (no crash, just wrong).
    static std::vector<std::vector<geo::Vec3>> polygonizeText(const QString& text, double height);

private:
    QLineEdit* textEdit_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QDoubleSpinBox* extrusionSpin_ = nullptr;
};

}  // namespace plnr::ui
