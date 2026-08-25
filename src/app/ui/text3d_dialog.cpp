#include "ui/text3d_dialog.h"

#include <utility>

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QLineEdit>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QVBoxLayout>

namespace plnr::ui {

namespace {

// Shoelace signed area of poly, in painter space (Y-down -- the sign is only
// ever compared to another subpath's sign below, never interpreted against
// the usual Y-up math convention, so the flip doesn't matter here).
double signedArea(const QPolygonF& poly) {
    double area = 0.0;
    const int n = poly.size();
    for (int i = 0; i < n; ++i) {
        const QPointF& p0 = poly[i];
        const QPointF& p1 = poly[(i + 1) % n];
        area += p0.x() * p1.y() - p1.x() * p0.y();
    }
    return area * 0.5;
}

}  // namespace

Text3dDialog::Text3dDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("3D Text"));

    textEdit_ = new QLineEdit(QStringLiteral("3D Text"), this);

    heightSpin_ = new QDoubleSpinBox(this);
    heightSpin_->setRange(0.1, 1000.0);
    heightSpin_->setSingleStep(0.1);
    heightSpin_->setValue(1.0);
    heightSpin_->setSuffix(QStringLiteral(" m"));

    extrusionSpin_ = new QDoubleSpinBox(this);
    extrusionSpin_->setRange(0.0, 1000.0);  // 0 = flat (no extrusion)
    extrusionSpin_->setSingleStep(0.05);
    extrusionSpin_->setValue(0.25);
    extrusionSpin_->setSuffix(QStringLiteral(" m"));

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Text"), textEdit_);
    form->addRow(QStringLiteral("Height"), heightSpin_);
    form->addRow(QStringLiteral("Extrusion"), extrusionSpin_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

QString Text3dDialog::text() const {
    return textEdit_->text();
}

double Text3dDialog::extrusion() const {
    return extrusionSpin_->value();
}

std::vector<std::vector<geo::Vec3>> Text3dDialog::outlines() const {
    return polygonizeText(textEdit_->text(), heightSpin_->value());
}

std::vector<std::vector<geo::Vec3>> Text3dDialog::polygonizeText(const QString& text, double height) {
    std::vector<std::vector<geo::Vec3>> result;
    if (text.isEmpty() || height <= 0.0) {
        return result;
    }

    // Reference point size is arbitrary -- polygonized coordinates are
    // rescaled below so the font's ascent maps to `height`, so the absolute
    // size chosen for QFont only affects hinting/precision, not the result.
    QFont font;
    font.setPointSizeF(200.0);

    QPainterPath path;
    path.addText(0.0, 0.0, font, text);

    const QFontMetricsF metrics(font);
    const double ascent = metrics.ascent();
    if (ascent <= 0.0) {
        return result;  // defensive: shouldn't happen for any real QFont
    }
    const double scale = height / ascent;

    const QList<QPolygonF> subpaths = path.toSubpathPolygons();
    if (subpaths.isEmpty()) {
        return result;  // e.g. text is all whitespace -- nothing to build
    }

    // Determine outer-vs-hole winding: sum each sign's total area across
    // subpaths (letters' outer loops dominate; hole loops are much smaller)
    // and keep whichever sign wins -- see polygonizeText's own header comment.
    double positiveArea = 0.0;
    double negativeArea = 0.0;
    for (const QPolygonF& sub : subpaths) {
        const double area = signedArea(sub);
        if (area >= 0.0) {
            positiveArea += area;
        } else {
            negativeArea += -area;
        }
    }
    const bool keepPositive = positiveArea >= negativeArea;

    for (const QPolygonF& sub : subpaths) {
        const double area = signedArea(sub);
        const bool isPositive = area >= 0.0;
        if (isPositive != keepPositive) {
            continue;  // hole subpath -- dropped (MVP cut, see header comment)
        }

        std::vector<geo::Vec3> outline;
        outline.reserve(static_cast<std::size_t>(sub.size()));
        for (const QPointF& pt : sub) {
            // Painter space is Y-down; model space is Y-up -- flip. z = 0:
            // 3D Text glyphs are built flat in the XY plane before
            // GeometryApi::add3dText extrudes them.
            outline.push_back(geo::Vec3{pt.x() * scale, -pt.y() * scale, 0.0});
        }

        // toSubpathPolygons() closes each subpath by repeating its first
        // point as last -- drop the duplicate before add3dText's own closing
        // edge, or it'll sit on a zero-length one.
        if (outline.size() > 1 && geo::length(outline.back() - outline.front()) < geo::kEps) {
            outline.pop_back();
        }
        if (outline.size() < 3) {
            continue;  // degenerate after dedup
        }

        result.push_back(std::move(outline));
    }

    return result;
}

}  // namespace plnr::ui
