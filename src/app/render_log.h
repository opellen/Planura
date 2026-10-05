#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QTextStream>

namespace plnr {

// Render-path diagnostic log: planura_render.log next to the exe, truncated
// once per process start. Event-driven call sites only, never per-frame.
inline void renderLog(const QString& line) {
    static const QString path =
        QCoreApplication::applicationDirPath() + QStringLiteral("/planura_render.log");
    static bool truncatedOnce = false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | (truncatedOnce ? QIODevice::Append : QIODevice::Truncate))) {
        return;  // logging must never break the app -- drop the line silently
    }
    truncatedOnce = true;
    QTextStream s(&f);
    s << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << ' ' << line << '\n';
}

}  // namespace plnr
