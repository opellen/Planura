#include "harness/probe_support.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QVector4D>

#include <cmath>
#include <cstdio>
#include <vector>

#include <geo/model.h>
#include <ordo/core/kernel.h>

#include "agent/geometry_api.h"
#include "viewport/camera.h"
#include "viewport/viewport_widget.h"

bool probeCheck(const char* name, bool ok, const QString& detail) {
    if (ok) {
        std::printf("PASS: %s\n", name);
        std::fflush(stdout);
    } else {
        std::fprintf(stderr, "FAIL: %s -- %s\n", name, detail.toLocal8Bit().constData());
        std::fflush(stderr);
    }
    return ok;
}

// processEvents() only settles synchronous fallout; timers need real time, so loop on a wall clock.
// Fixed duration -- scenarios never poll and retry.
void pumpEventsFor(int ms) {
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
    } while (timer.elapsed() < ms);
}

QString probeCaptureDir() {
    // The exe lives under <repo>/build/...; walk up to the dir holding tools.ps1 and src/.
    QDir dir(QCoreApplication::applicationDirPath());
    while (!(dir.exists(QStringLiteral("tools.ps1")) && dir.exists(QStringLiteral("src")))) {
        if (!dir.cdUp()) break;
    }
    const QString path = dir.filePath(QStringLiteral("temp/captures/planura-probe"));
    QDir().mkpath(path);
    return path;
}

ProjectedPoint projectWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world) {
    const plnr::viewport::Camera& cam = viewport->camera();
    const int w = viewport->width();
    const int h = viewport->height();
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    const QMatrix4x4 v1 = cam.projectionMatrix(aspect) * cam.viewMatrix();
    const QVector4D clip =
        v1 * QVector4D(static_cast<float>(world.x), static_cast<float>(world.y), static_cast<float>(world.z), 1.0f);

    ProjectedPoint out;
    if (std::abs(clip.w()) > 1e-9f) {
        const float ndcX = clip.x() / clip.w();
        const float ndcY = clip.y() / clip.w();
        const float ndcZ = clip.z() / clip.w();
        out.widget = QPointF((ndcX * 0.5f + 0.5f) * static_cast<float>(w),
                             (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(h));  // Qt pixel space: y flipped
        out.visible = clip.w() > 0.0f && ndcZ >= -1.0f && ndcZ <= 1.0f && ndcX >= -1.0f && ndcX <= 1.0f &&
                      ndcY >= -1.0f && ndcY <= 1.0f;
    }
    return out;
}

namespace {

// Mirrors ToolController's double-click detection (5 px distance, doubleClickInterval()).
constexpr qreal kClickFuseDistancePx = 5.0;

struct LastClick {
    QPointF pos;
    QElapsedTimer timer;
};

LastClick& lastClick() {
    static LastClick last;
    return last;
}

// Pumps past the double-click interval when `pos` would fuse with the previous click.
void avoidDoubleClickFusion(const QPointF& pos) {
    const LastClick& last = lastClick();
    if (!last.timer.isValid()) return;
    const qreal dx = pos.x() - last.pos.x();
    const qreal dy = pos.y() - last.pos.y();
    if (std::sqrt(dx * dx + dy * dy) > kClickFuseDistancePx) return;
    const int interval = QApplication::doubleClickInterval();
    const qint64 elapsed = last.timer.elapsed();
    if (elapsed <= interval) pumpEventsFor(static_cast<int>(interval - elapsed) + 20);
}

void recordClick(const QPointF& pos) {
    LastClick& last = lastClick();
    last.pos = pos;
    last.timer.start();
}

const plnr::geo::Model* modelOf(ordo::core::Kernel& kernel) {
    auto api = kernel.agentAs<plnr::agent::GeometryApi>(plnr::agent::kGeometryApiName);
    return api ? &api->model() : nullptr;
}

}  // namespace

bool clickWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world,
                Qt::KeyboardModifiers modifiers) {
    const ProjectedPoint p = projectWorld(viewport, world);
    if (!p.visible) return false;
    avoidDoubleClickFusion(p.widget);
    viewport->debugMousePress(p.widget, Qt::LeftButton, modifiers);
    viewport->debugMouseRelease(p.widget, Qt::LeftButton, modifiers);
    recordClick(p.widget);
    return true;
}

bool moveWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world,
               Qt::KeyboardModifiers modifiers) {
    const ProjectedPoint p = projectWorld(viewport, world);
    if (!p.visible) return false;
    viewport->debugMouseMove(p.widget, Qt::NoButton, modifiers);
    return true;
}

bool doubleClickWorld(plnr::viewport::ViewportWidget* viewport, const plnr::geo::Vec3& world,
                      Qt::KeyboardModifiers modifiers) {
    const ProjectedPoint p = projectWorld(viewport, world);
    if (!p.visible) return false;
    avoidDoubleClickFusion(p.widget);
    viewport->debugMousePress(p.widget, Qt::LeftButton, modifiers);
    viewport->debugMouseRelease(p.widget, Qt::LeftButton, modifiers);
    viewport->debugMouseDoubleClick(p.widget, Qt::LeftButton, modifiers);
    viewport->debugMouseRelease(p.widget, Qt::LeftButton, modifiers);
    recordClick(p.widget);
    return true;
}

std::optional<plnr::geo::Vec3> worldOfVertex(ordo::core::Kernel& kernel, plnr::geo::Id id) {
    const plnr::geo::Model* model = modelOf(kernel);
    const plnr::geo::Vertex* v = model ? model->vertex(id) : nullptr;
    if (!v) return std::nullopt;
    return v->pos;
}

std::optional<plnr::geo::Vec3> worldOfEdgeMidpoint(ordo::core::Kernel& kernel, plnr::geo::Id id) {
    const plnr::geo::Model* model = modelOf(kernel);
    const plnr::geo::Edge* e = model ? model->edge(id) : nullptr;
    if (!e) return std::nullopt;
    const plnr::geo::HalfEdge* he0 = model->halfEdge(e->halfEdges[0]);
    const plnr::geo::HalfEdge* he1 = model->halfEdge(e->halfEdges[1]);
    if (!he0 || !he1) return std::nullopt;
    const plnr::geo::Vertex* a = model->vertex(he0->origin);
    const plnr::geo::Vertex* b = model->vertex(he1->origin);
    if (!a || !b) return std::nullopt;
    return plnr::geo::Vec3{(a->pos.x + b->pos.x) * 0.5, (a->pos.y + b->pos.y) * 0.5, (a->pos.z + b->pos.z) * 0.5};
}

std::optional<plnr::geo::Vec3> worldOfFaceCentroid(ordo::core::Kernel& kernel, plnr::geo::Id id) {
    const plnr::geo::Model* model = modelOf(kernel);
    if (!model || !model->face(id)) return std::nullopt;
    const std::vector<plnr::geo::Id> loop = model->faceVertexLoop(id);
    if (loop.empty()) return std::nullopt;
    plnr::geo::Vec3 sum{};
    for (plnr::geo::Id vid : loop) {
        const plnr::geo::Vertex* v = model->vertex(vid);
        if (!v) return std::nullopt;
        sum = {sum.x + v->pos.x, sum.y + v->pos.y, sum.z + v->pos.z};
    }
    const double n = static_cast<double>(loop.size());
    return plnr::geo::Vec3{sum.x / n, sum.y / n, sum.z / n};
}
