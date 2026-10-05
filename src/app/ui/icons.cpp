#include "ui/icons.h"

#include <QHash>
#include <QPainter>
#include <QSvgRenderer>
#include <QtGlobal>

// One cached QSvgRenderer per slug, integer-size rasterization. No tinting: the icons are
// multi-color artwork drawn for white backgrounds, rendered as authored. Disabled is left to
// Qt's default generation.

namespace plnr::ui::icons {

namespace {

// One renderer per slug, kept for the process lifetime. A null entry
// records a slug that failed to load, so the warning fires once.
QSvgRenderer* rendererFor(const QString& slug) {
    static QHash<QString, QSvgRenderer*> renderers;
    auto it = renderers.constFind(slug);
    if (it != renderers.constEnd()) {
        return it.value();
    }
    auto* renderer = new QSvgRenderer(QStringLiteral(":/icons/%1.svg").arg(slug));
    if (!renderer->isValid()) {
        qWarning("icons: missing or invalid icon slug '%s'", qPrintable(slug));
        delete renderer;
        renderer = nullptr;
    }
    renderers.insert(slug, renderer);
    return renderer;
}

QPixmap render(QSvgRenderer* renderer, int px) {
    QPixmap result(px, px);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer->render(&painter, QRectF(0, 0, px, px));
    return result;
}

}  // namespace

QIcon icon(const QString& slug) {
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(slug);
    if (it != cache.constEnd()) {
        return it.value();
    }
    QIcon result;
    if (QSvgRenderer* renderer = rendererFor(slug)) {
        // Several sizes let Qt pick a crisp match for any icon size x DPR.
        for (int size : {16, 20, 24, 32, 48, 64}) {
            result.addPixmap(render(renderer, size), QIcon::Normal, QIcon::Off);
        }
    }
    cache.insert(slug, result);
    return result;
}

QPixmap pixmap(const QString& slug, int sizePx, qreal dpr) {
    QSvgRenderer* renderer = rendererFor(slug);
    if (!renderer) {
        return QPixmap();
    }
    QPixmap result = render(renderer, qRound(sizePx * dpr));
    result.setDevicePixelRatio(dpr);
    return result;
}

}  // namespace plnr::ui::icons
