#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace plnr::ui::icons {

// Icon for a :/icons/<slug>.svg resource, cached per slug. Rasterized
// untinted at 16/20/24/32/48/64 px (Normal/Off only; Qt derives Disabled).
// Returns a null QIcon (and warns once per slug) if the slug is missing.
QIcon icon(const QString& slug);

// Untinted pixmap of the slug at sizePx logical pixels, rendered at
// sizePx * dpr physical pixels with the pixmap's DPR set to dpr.
// Null pixmap if the slug is missing.
QPixmap pixmap(const QString& slug, int sizePx, qreal dpr);

}  // namespace plnr::ui::icons
