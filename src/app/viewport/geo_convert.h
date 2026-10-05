#pragma once

// Value conversions between Qt viewport types (QVector3D, float) and kernel
// geo types (geo::Vec3, double). Standalone header so Qt-only consumers
// (e.g. plnr_camera_test) need not link plnr_geo, and vice versa.

#include <QVector3D>

#include <geo/vec3.h>

namespace plnr::viewport {

inline QVector3D toQt(const geo::Vec3& v) {
    return QVector3D(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

inline geo::Vec3 toGeo(const QVector3D& v) {
    return geo::Vec3{static_cast<double>(v.x()), static_cast<double>(v.y()), static_cast<double>(v.z())};
}

}  // namespace plnr::viewport
