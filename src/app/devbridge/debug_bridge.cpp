// Protocol: newline-delimited JSON over a plain TCP socket on
// 127.0.0.1:<port>, one JSON object per line each direction.
//   Request:  {"id":<any>,"cmd":"<name>", ...params}
//   Response: {"id":<echoed>,"ok":true,...} or {"id":<echoed>,"ok":false,"error":"..."}
// "id" is optional on the request (echoed back as -1 if absent) and is
// otherwise unexamined. Multiple clients may connect concurrently.

// Query commands (never mutate domain state):

//   ping                       -> {app, protocol}

//   scene                      -> {vertices:[{id,x,y,z,hidden,tag}],
//                                   edges:[{id,v1,v2,hidden,tag}],
//                                   faces:[{id,loop:[ids],normal:{x,y,z},hidden,tag}],
//                                   instances:[{id,definitionId,name,isGroup,
//                                     translation:{x,y,z},edgeCount,faceCount}]}
//       tag: Untagged id when unmapped/no TagStore. instances: root's direct children only.

//   camera                     -> {azimuthDeg,elevationDeg,distance,fovYDeg,
//                                   target:{x,y,z},viewportW,viewportH}

//   camera_set {azimuthDeg?,elevationDeg?,distance?,fovYDeg?,target?:{x?,y?,z?}}
//                               -> camera's own shape. Omitted fields keep
//       their value. Via CameraStore::restoreCamera -- lets a scenario pin
//       its authoring pose independent of the app's default.

//   status                     -> {hint, tool}

//   selection                  -> {items:[{kind,id}...]}, in selection order.

//   edit_context                -> {path:[instanceId...], atRoot}
//       path is the "enter" chain from the root; [] + atRoot:true at the
//       root or when EditContextStore is absent.

//   entity_info                 -> {text} -- Entity Info's live summary label.

//   vcb                        -> {label, text} -- VCB (Measurements Box)
//       read-only mirror; no command yet types into/commits the field.

//   overlay_stats                -> viewport overlay-buffer diagnostics (see cmdOverlayStats).

//   tags                        -> {tags:[{id,name,visible}],
//                                    assignments:[{kind,entityId,tagId}]}
//       assignments holds only entities explicitly off the Untagged tag.

//   pick {x,y}                  -> {kind,entityId,point:{x,y,z},depth,instanceId}
//       kind: "vertex"|"edge"|"face"|"none". entityId (not "id" -- collides
//       with the envelope's own "id"). instanceId is the top-level group/
//       component the hit belongs to, 0 for a root hit or no hit -- matches
//       what the Select tool sees.

//   infer {x,y}                  -> {kind,pos:{x,y,z},refId}
//       kind: "endpoint"|"midpoint"|"onedge"|"onface"|"intersection"|
//       "guidepoint"|"guideline"|"frompoint"|"parallel"|"perpendicular"|
//       "onaxis"|"ground"|"none".

//   project {x,y,z}              -> {x, y, visible}

//   screenshot {path}            -> {saved}

//   events {since?}              -> {events:[{seq,name,subscribers,tMs}...], nextSince}
//       Tails the Dispatcher's last-100-entry trace ring buffer; returns
//       seq > since (default 0). Poll with since:nextSince to avoid
//       re-fetching entries already seen, even across eviction.

// Injection commands (viewport-local pixel coords, Qt top-left origin;
// mutate domain state only via the same input path a human would use):

//   mouse_press/mouse_move/mouse_release {x,y,button?,modifiers?}
//       button: "left"(default)|"middle"|"right". modifiers: array of
//       "shift"|"ctrl"|"alt"|"meta", default []. -> {posted:true}

//   key {key,modifiers?}          -> {posted:true}
//       key: a QKeySequence-parseable string ("Escape","Shift+Z",...), or
//       one of "Ctrl"/"Control"/"Shift"/"Alt"/"Meta" (case-insensitive) for
//       a BARE standalone modifier KeyPress (e.g. Flip's Ctrl copy-mode
//       toggle) -- special-cased ahead of the QKeySequence parse.

//   tool {name}                   -> {tool: <active tool after the switch>}
//       name: an events::ToolId name -- "Select"|"Line"|"Eraser"|"Move"|"Rectangle"|"RotatedRectangle"|
//       "PushPull"|"Circle"|"Polygon"|"Arc2Point"|"Arc3Point"|"ArcCenter"|"Pie"|"Freehand"|"Rotate"|"Scale"|
//       "Offset"|"FollowMe"|"Flip"|"TapeMeasure"|"Protractor"|"Axes"|"Dimension"|"Text"|"Text3D"|"SectionPlane"|
//       "PaintBucket"|"OuterShell"|"SolidUnion"|"SolidSubtract"|"SolidTrim"|"SolidIntersect"|"SolidSplit".
//       Solid Tools names are menu-only; "Text3D" opens Text3dDialog (non-blocking) instead of activating.

//   menu_action {menu,text}        -> {triggered:true}
//       Finds the QAction under menu bar entry `menu` whose text matches
//       `text` (searches submenus) and triggers it.

//   modal {action,...}            Scripts ToolController's confirm/prompt/warning
//       dialogs so a scenario run never opens a real, unscriptable Qt dialog.
//       action:"queue" {kind,accepted?,text?} -> {queued:true}
//           kind: "confirm"|"prompt"|"warning"; accepted default true, text default "".
//       action:"status" -> {queueLength, lastModal:{kind,message}|null}

//   context_menu {action,...}     Drives ToolController::buildContextMenuItems
//       directly -- never opens a real QMenu (QMenu::exec() would block the
//       event loop like a modal dialog).
//       action:"list" {x,y} -> {items:[{label,checkable,checked}...]}
//       action:"trigger" {x,y,label} -> {triggered} -- false (not an error)
//           when no item matches; invokes the item's action() closure.

//   doc {action,...}              kernel_.send exception (bypasses confirmDiscardChanges()
//       and the Open/Save QFileDialogs -- no real dialog may appear in a scenario run).
//       action:"new" -> NewDocumentRequested, then "state". "open"/"save"/"export_obj"/
//       "import_obj" {path} -> matching Requested event, then "state".
//       action:"state" -> {filePath, dirty, units, windowTitle}

//   tag {action,...}              kernel_.send exception (TagsPresenter's
//       Tray-dock widgets carry no QAction at all).
//       action:"create" {name?} -> {created:true} -- empty name auto-names "Tag N"
//       action:"assign" {kind,entityId,tagId} -> {assigned:true}
//           entityId, not "id" (envelope collision). Unknown tagId is a
//           silent no-op -- confirm via `tags` afterward.

//   material {action,...}         kernel_.send exception (no QAction path exists yet).
//       create{name?,r,g,b,opacity?}->{materialId}  paint{kind,entityId,materialId}->{painted}
//       set_active{materialId}->{materialId}  sample{kind,entityId}->{materialId} (read-only)
//       set_texture{materialId,path,tileW?,tileH?}->{materialId,textured} (path empty clears)
//       set_uv_transform{kind,entityId,du?,dv?,rot?,su?,sv?}->{du,dv,rot,su,sv} (identity defaults)
//       list->{materials:[{id,name,rgba,opacity,assetHash,tileW,tileH}],assignments:[[kind,id,front,back,uv?]],activeId}

//   style {action,...}            QAction::trigger() path (View menu); set_ao_strength
//       is the one kernel_.send exception (no tray panel yet).
//       set_face_style{style}  style: "wireframe"|"hiddenLine"|"shaded"|"shadedWithTextures"|"monochrome"|"xray"
//       set_edge_flag{flag,value}  flag: "profiles"|"depthCue"|"backEdges"
//       set_ambient_occlusion{ambientOcclusion}  set_ao_strength{aoStrength}
//       state -> {faceStyle,profiles,depthCue,backEdges,ambientOcclusion,aoStrength,frontColor:[r,g,b],backColor:[r,g,b]}

//   shadow {action,...}           Hybrid: set_show_shadows/set_use_sun_for_shading trigger
//       QActions; the rest are kernel_.send exceptions (no Shadows tray panel yet).
//       set_show_shadows{showShadows}  set_use_sun_for_shading{useSunForShading}
//       set_position{latitudeDeg,longitudeDeg}  set_datetime{month,day,hourLocal(0-24)}
//       set_light{light}/set_dark{dark} -- both 0-100
//       state -> {useSunForShading,showShadows,latitudeDeg,longitudeDeg,month,day,hourLocal,light,dark}

//   fog {action,...}              Hybrid like shadow: set_enabled triggers a
//       QAction; set_range/set_use_background_color are kernel_.send exceptions.
//       set_enabled{enabled}  set_range{startDistance,endDistance}
//       set_use_background_color{useBackgroundColor}
//       state -> {enabled,startDistance,endDistance,useBackgroundColor,color:[r,g,b]}

//   solid {action?,instanceId}    action:"query" (default) -- read-only.
//       -> {found,solid,wireEdges,nonManifoldVertex,nonPositiveVolume,
//       modelVolume,worldVolume,instanceCount}; found:false (no other
//       fields) when instanceId isn't a live root Instance.

//   solid {action:"apply",op,instanceIds}  kernel_.send exception. op:
//       "union"|"outerShell"|"subtract"|"trim"|"intersect"|"split".
//       -> {applied,newInstanceIds} -- root Instance ids new since the send
//       (before/after snapshot, since a rejected op mutates nothing);
//       applied is whether that list is non-empty.

#include "debug_bridge.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <ios>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include <QAction>
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMatrix4x4>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVector3D>
#include <QVector4D>

#include <geo/infer.h>
#include <geo/model.h>
#include <geo/pick.h>
#include <geo/scene_pick.h>
#include <geo/solid.h>

#include "agent/camera_store.h"
#include "agent/document_store.h"
#include "agent/edit_context_store.h"
#include "agent/geometry_api.h"
#include "agent/material_repository.h"
#include "agent/selection_store.h"
#include "agent/fog_store.h"
#include "agent/shadow_store.h"
#include "agent/style_store.h"
#include "agent/tag_store.h"
#include "main_window.h"
#include "tools/tool_controller.h"
#include "ui/context_menu.h"
#include "viewport/camera.h"
#include "viewport/viewport_widget.h"

namespace plnr::devbridge {

namespace {

// Thrown by cmd* handlers on any recognized failure (unknown command,
// missing/malformed param); execute()'s caller (handleLine) catches it and
// turns it into an {"ok":false,"error":...} response line.
struct CommandError {
    QString message;
};

double requireNumber(const QJsonObject& req, const char* key) {
    const QJsonValue value = req.value(QLatin1String(key));
    if (!value.isDouble()) {
        throw CommandError{QStringLiteral("missing or non-numeric param '%1'").arg(QLatin1String(key))};
    }
    return value.toDouble();
}

QString requireString(const QJsonObject& req, const char* key) {
    const QJsonValue value = req.value(QLatin1String(key));
    if (!value.isString()) {
        throw CommandError{QStringLiteral("missing or non-string param '%1'").arg(QLatin1String(key))};
    }
    return value.toString();
}

// First boolean-typed required param this bridge has needed -- same
// missing-or-wrong-type-throws discipline as requireNumber/requireString
// above.
bool requireBool(const QJsonObject& req, const char* key) {
    const QJsonValue value = req.value(QLatin1String(key));
    if (!value.isBool()) {
        throw CommandError{QStringLiteral("missing or non-boolean param '%1'").arg(QLatin1String(key))};
    }
    return value.toBool();
}

QJsonObject vec3ToJson(const geo::Vec3& v) {
    QJsonObject o;
    o["x"] = v.x;
    o["y"] = v.y;
    o["z"] = v.z;
    return o;
}

QString pickKindToString(geo::PickKind kind) {
    switch (kind) {
        case geo::PickKind::Vertex: return QStringLiteral("vertex");
        case geo::PickKind::Edge: return QStringLiteral("edge");
        case geo::PickKind::Face: return QStringLiteral("face");
        case geo::PickKind::None: break;
    }
    return QStringLiteral("none");
}

QString entityKindToString(geo::EntityKind kind) {
    switch (kind) {
        case geo::EntityKind::Vertex: return QStringLiteral("Vertex");
        case geo::EntityKind::Edge: return QStringLiteral("Edge");
        case geo::EntityKind::Face: return QStringLiteral("Face");
        case geo::EntityKind::Instance: return QStringLiteral("Instance");
    }
    return QStringLiteral("Vertex");
}

// Reverse of entityKindToString above -- backs cmdTag's "assign" action.
// ok=false when name doesn't match any geo::EntityKind.
geo::EntityKind entityKindFromString(const QString& name, bool& ok) {
    ok = true;
    if (name == QStringLiteral("Vertex")) return geo::EntityKind::Vertex;
    if (name == QStringLiteral("Edge")) return geo::EntityKind::Edge;
    if (name == QStringLiteral("Face")) return geo::EntityKind::Face;
    if (name == QStringLiteral("Instance")) return geo::EntityKind::Instance;
    ok = false;
    return geo::EntityKind::Vertex;
}

QString inferenceKindToString(geo::InferenceKind kind) {
    switch (kind) {
        case geo::InferenceKind::None: return QStringLiteral("none");
        case geo::InferenceKind::Endpoint: return QStringLiteral("endpoint");
        case geo::InferenceKind::Midpoint: return QStringLiteral("midpoint");
        case geo::InferenceKind::OnEdge: return QStringLiteral("onedge");
        case geo::InferenceKind::OnFace: return QStringLiteral("onface");
        case geo::InferenceKind::Intersection: return QStringLiteral("intersection");
        case geo::InferenceKind::GuidePoint: return QStringLiteral("guidepoint");
        case geo::InferenceKind::GuideLine: return QStringLiteral("guideline");
        case geo::InferenceKind::FromPoint: return QStringLiteral("frompoint");
        case geo::InferenceKind::Parallel: return QStringLiteral("parallel");
        case geo::InferenceKind::Perpendicular: return QStringLiteral("perpendicular");
        case geo::InferenceKind::OnAxis: return QStringLiteral("onaxis");
        case geo::InferenceKind::GroundPlane: return QStringLiteral("ground");
    }
    return QStringLiteral("none");
}

QString toolIdToString(events::ToolId tool) {
    switch (tool) {
        case events::ToolId::Select: return QStringLiteral("Select");
        case events::ToolId::Line: return QStringLiteral("Line");
        case events::ToolId::Eraser: return QStringLiteral("Eraser");
        case events::ToolId::Move: return QStringLiteral("Move");
        case events::ToolId::Rectangle: return QStringLiteral("Rectangle");
        case events::ToolId::PushPull: return QStringLiteral("PushPull");
        case events::ToolId::Circle: return QStringLiteral("Circle");
        case events::ToolId::Polygon: return QStringLiteral("Polygon");
        case events::ToolId::Arc2Point: return QStringLiteral("Arc2Point");
        case events::ToolId::Arc3Point: return QStringLiteral("Arc3Point");
        case events::ToolId::ArcCenter: return QStringLiteral("ArcCenter");
        case events::ToolId::Pie: return QStringLiteral("Pie");
        case events::ToolId::Freehand: return QStringLiteral("Freehand");
        case events::ToolId::RotatedRectangle: return QStringLiteral("RotatedRectangle");
        case events::ToolId::Rotate: return QStringLiteral("Rotate");
        case events::ToolId::Scale: return QStringLiteral("Scale");
        case events::ToolId::Offset: return QStringLiteral("Offset");
        case events::ToolId::FollowMe: return QStringLiteral("FollowMe");
        case events::ToolId::Flip: return QStringLiteral("Flip");
        case events::ToolId::TapeMeasure: return QStringLiteral("TapeMeasure");
        case events::ToolId::Protractor: return QStringLiteral("Protractor");
        case events::ToolId::Axes: return QStringLiteral("Axes");
        case events::ToolId::Dimension: return QStringLiteral("Dimension");
        case events::ToolId::Text: return QStringLiteral("Text");
        case events::ToolId::Text3D: return QStringLiteral("Text3D");
        case events::ToolId::SectionPlane: return QStringLiteral("SectionPlane");
        case events::ToolId::PaintBucket: return QStringLiteral("PaintBucket");
        case events::ToolId::OuterShell: return QStringLiteral("OuterShell");
        case events::ToolId::SolidUnion: return QStringLiteral("SolidUnion");
        case events::ToolId::SolidSubtract: return QStringLiteral("SolidSubtract");
        case events::ToolId::SolidTrim: return QStringLiteral("SolidTrim");
        case events::ToolId::SolidIntersect: return QStringLiteral("SolidIntersect");
        case events::ToolId::SolidSplit: return QStringLiteral("SolidSplit");
    }
    return QStringLiteral("Select");
}

// ok is set false (and the return value is meaningless) when name doesn't
// match any plnr::events::ToolId.
events::ToolId toolIdFromString(const QString& name, bool& ok) {
    ok = true;
    if (name == QStringLiteral("Select")) return events::ToolId::Select;
    if (name == QStringLiteral("Line")) return events::ToolId::Line;
    if (name == QStringLiteral("Eraser")) return events::ToolId::Eraser;
    if (name == QStringLiteral("Move")) return events::ToolId::Move;
    if (name == QStringLiteral("Rectangle")) return events::ToolId::Rectangle;
    if (name == QStringLiteral("PushPull")) return events::ToolId::PushPull;
    if (name == QStringLiteral("Circle")) return events::ToolId::Circle;
    if (name == QStringLiteral("Polygon")) return events::ToolId::Polygon;
    if (name == QStringLiteral("Arc2Point")) return events::ToolId::Arc2Point;
    if (name == QStringLiteral("Arc3Point")) return events::ToolId::Arc3Point;
    if (name == QStringLiteral("ArcCenter")) return events::ToolId::ArcCenter;
    if (name == QStringLiteral("Pie")) return events::ToolId::Pie;
    if (name == QStringLiteral("Freehand")) return events::ToolId::Freehand;
    if (name == QStringLiteral("RotatedRectangle")) return events::ToolId::RotatedRectangle;
    if (name == QStringLiteral("Rotate")) return events::ToolId::Rotate;
    if (name == QStringLiteral("Scale")) return events::ToolId::Scale;
    if (name == QStringLiteral("Offset")) return events::ToolId::Offset;
    if (name == QStringLiteral("FollowMe")) return events::ToolId::FollowMe;
    if (name == QStringLiteral("Flip")) return events::ToolId::Flip;
    if (name == QStringLiteral("TapeMeasure")) return events::ToolId::TapeMeasure;
    if (name == QStringLiteral("Protractor")) return events::ToolId::Protractor;
    if (name == QStringLiteral("Axes")) return events::ToolId::Axes;
    if (name == QStringLiteral("Dimension")) return events::ToolId::Dimension;
    if (name == QStringLiteral("Text")) return events::ToolId::Text;
    if (name == QStringLiteral("Text3D")) return events::ToolId::Text3D;
    if (name == QStringLiteral("SectionPlane")) return events::ToolId::SectionPlane;
    if (name == QStringLiteral("PaintBucket")) return events::ToolId::PaintBucket;
    if (name == QStringLiteral("OuterShell")) return events::ToolId::OuterShell;
    if (name == QStringLiteral("SolidUnion")) return events::ToolId::SolidUnion;
    if (name == QStringLiteral("SolidSubtract")) return events::ToolId::SolidSubtract;
    if (name == QStringLiteral("SolidTrim")) return events::ToolId::SolidTrim;
    if (name == QStringLiteral("SolidIntersect")) return events::ToolId::SolidIntersect;
    if (name == QStringLiteral("SolidSplit")) return events::ToolId::SolidSplit;
    ok = false;
    return events::ToolId::Select;
}

// lowerCamelCase string mapping for events::SolidOp -- backs `solid`'s op field.
QString solidOpToString(events::SolidOp op) {
    switch (op) {
        case events::SolidOp::Union: return QStringLiteral("union");
        case events::SolidOp::OuterShell: return QStringLiteral("outerShell");
        case events::SolidOp::Subtract: return QStringLiteral("subtract");
        case events::SolidOp::Trim: return QStringLiteral("trim");
        case events::SolidOp::Intersect: return QStringLiteral("intersect");
        case events::SolidOp::Split: return QStringLiteral("split");
    }
    return QStringLiteral("union");  // unreachable -- SolidOp is exhaustively handled above
}

// ok is set false (and the return value is meaningless) when name doesn't
// match any events::SolidOp -- same ok-out-param convention as
// toolIdFromString above.
events::SolidOp solidOpFromString(const QString& name, bool& ok) {
    ok = true;
    if (name == QStringLiteral("union")) return events::SolidOp::Union;
    if (name == QStringLiteral("outerShell")) return events::SolidOp::OuterShell;
    if (name == QStringLiteral("subtract")) return events::SolidOp::Subtract;
    if (name == QStringLiteral("trim")) return events::SolidOp::Trim;
    if (name == QStringLiteral("intersect")) return events::SolidOp::Intersect;
    if (name == QStringLiteral("split")) return events::SolidOp::Split;
    ok = false;
    return events::SolidOp::Union;
}

// Same strings as io/plr_writer.cpp's faceStyleToString (.plr `style.faceStyle`) --
// keep in sync so a scenario can assert style_set_face_style() against the saved JSON.
QString faceStyleToString(events::FaceStyle style) {
    switch (style) {
        case events::FaceStyle::Wireframe: return QStringLiteral("wireframe");
        case events::FaceStyle::HiddenLine: return QStringLiteral("hiddenLine");
        case events::FaceStyle::Shaded: return QStringLiteral("shaded");
        case events::FaceStyle::ShadedWithTextures: return QStringLiteral("shadedWithTextures");
        case events::FaceStyle::Monochrome: return QStringLiteral("monochrome");
        case events::FaceStyle::XRay: return QStringLiteral("xray");
    }
    return QStringLiteral("shadedWithTextures");  // unreachable -- FaceStyle is exhaustively handled above
}

// ok is set false (and the return value is meaningless) when name doesn't
// match any plnr::events::FaceStyle -- same ok-out-param convention as
// toolIdFromString above.
events::FaceStyle faceStyleFromString(const QString& name, bool& ok) {
    ok = true;
    if (name == QStringLiteral("wireframe")) return events::FaceStyle::Wireframe;
    if (name == QStringLiteral("hiddenLine")) return events::FaceStyle::HiddenLine;
    if (name == QStringLiteral("shaded")) return events::FaceStyle::Shaded;
    if (name == QStringLiteral("shadedWithTextures")) return events::FaceStyle::ShadedWithTextures;
    if (name == QStringLiteral("monochrome")) return events::FaceStyle::Monochrome;
    if (name == QStringLiteral("xray")) return events::FaceStyle::XRay;
    ok = false;
    return events::FaceStyle::ShadedWithTextures;
}

QString edgeFlagToString(events::EdgeFlag flag) {
    switch (flag) {
        case events::EdgeFlag::Profiles: return QStringLiteral("profiles");
        case events::EdgeFlag::DepthCue: return QStringLiteral("depthCue");
        case events::EdgeFlag::BackEdges: return QStringLiteral("backEdges");
    }
    return QStringLiteral("profiles");  // unreachable -- EdgeFlag is exhaustively handled above
}

events::EdgeFlag edgeFlagFromString(const QString& name, bool& ok) {
    ok = true;
    if (name == QStringLiteral("profiles")) return events::EdgeFlag::Profiles;
    if (name == QStringLiteral("depthCue")) return events::EdgeFlag::DepthCue;
    if (name == QStringLiteral("backEdges")) return events::EdgeFlag::BackEdges;
    ok = false;
    return events::EdgeFlag::Profiles;
}

// Strips Qt's '&' mnemonic marker (e.g. "&Edit") for matching visible menu
// text. No app menu/action title contains a literal '&', so this is safe.
QString stripMnemonic(const QString& text) {
    QString result = text;
    result.remove(QLatin1Char('&'));
    return result;
}

// String mapping for ScriptedModalAnswer::Kind -- backs `modal`'s kind field.
QString modalKindToString(tools::ToolController::ScriptedModalAnswer::Kind kind) {
    switch (kind) {
        case tools::ToolController::ScriptedModalAnswer::Confirm: return QStringLiteral("confirm");
        case tools::ToolController::ScriptedModalAnswer::Prompt: return QStringLiteral("prompt");
        case tools::ToolController::ScriptedModalAnswer::Warning: return QStringLiteral("warning");
    }
    return QStringLiteral("warning");
}

// ok is set false (and the return value is meaningless) when name doesn't
// match any ScriptedModalAnswer::Kind -- same ok-out-param convention as
// toolIdFromString above.
tools::ToolController::ScriptedModalAnswer::Kind modalKindFromString(const QString& name, bool& ok) {
    ok = true;
    if (name == QStringLiteral("confirm")) return tools::ToolController::ScriptedModalAnswer::Confirm;
    if (name == QStringLiteral("prompt")) return tools::ToolController::ScriptedModalAnswer::Prompt;
    if (name == QStringLiteral("warning")) return tools::ToolController::ScriptedModalAnswer::Warning;
    ok = false;
    return tools::ToolController::ScriptedModalAnswer::Warning;
}

Qt::MouseButton buttonFromString(const QString& name) {
    const QString lower = name.toLower();
    if (lower == QStringLiteral("middle")) return Qt::MiddleButton;
    if (lower == QStringLiteral("right")) return Qt::RightButton;
    return Qt::LeftButton;  // default, and covers an explicit "left"
}

// Display name for a DispatchRecord: eventName if declared, else "0x"+hex(typeHash).
// typeHash is stable within a run but not across runs.
std::string traceRecordName(const ordo::core::DispatchRecord& record) {
    if (!record.eventName.empty()) {
        return std::string(record.eventName);
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << record.typeHash;
    return oss.str();
}

Qt::KeyboardModifiers modifiersFromJson(const QJsonArray& arr) {
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    for (const QJsonValue& v : arr) {
        const QString name = v.toString().toLower();
        if (name == QStringLiteral("shift")) {
            mods |= Qt::ShiftModifier;
        } else if (name == QStringLiteral("ctrl") || name == QStringLiteral("control")) {
            mods |= Qt::ControlModifier;
        } else if (name == QStringLiteral("alt")) {
            mods |= Qt::AltModifier;
        } else if (name == QStringLiteral("meta")) {
            mods |= Qt::MetaModifier;
        }
    }
    return mods;
}

// A bare-modifier KeyPress (e.g. Flip's Ctrl copy-mode toggle) -- cmdKey's
// {key, modifier} pair for it. Special-cased ahead of QKeySequence, whose
// key-name table also uses "Ctrl" for Qt::Key_Control itself.
struct BareModifierKey {
    Qt::Key key;
    Qt::KeyboardModifier modifier;
};

// nullopt when name isn't a bare-modifier name -- callers fall back to the
// ordinary QKeySequence parse otherwise.
std::optional<BareModifierKey> bareModifierKeyFromString(const QString& name) {
    const QString lower = name.toLower();
    if (lower == QStringLiteral("ctrl") || lower == QStringLiteral("control")) {
        return BareModifierKey{Qt::Key_Control, Qt::ControlModifier};
    }
    if (lower == QStringLiteral("shift")) {
        return BareModifierKey{Qt::Key_Shift, Qt::ShiftModifier};
    }
    if (lower == QStringLiteral("alt")) {
        return BareModifierKey{Qt::Key_Alt, Qt::AltModifier};
    }
    if (lower == QStringLiteral("meta")) {
        return BareModifierKey{Qt::Key_Meta, Qt::MetaModifier};
    }
    return std::nullopt;
}

}  // namespace

DebugBridge::DebugBridge(ordo::core::AppKernel& kernel, MainWindow* window, quint16 port, QObject* parent)
    : QObject(parent), kernel_(kernel), window_(window), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &DebugBridge::onNewConnection);
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "DebugBridge: failed to listen on 127.0.0.1:" << port << "--" << server_->errorString();
    } else {
        // Announces the resolved port (CLI arg / PLNR_BRIDGE_PORT /
        // kDefaultPort) so a developer running multiple instances side by
        // side can confirm which port this instance actually bound to.
        qInfo() << "DebugBridge: listening on 127.0.0.1:" << port;
    }

    traceClock_.start();
    kernel_.dispatcher().setObserver([this](const ordo::core::DispatchRecord& record) { onDispatchTrace(record); });
}

DebugBridge::~DebugBridge() {
    kernel_.dispatcher().setObserver({});
}

bool DebugBridge::isListening() const {
    return server_->isListening();
}

void DebugBridge::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QTcpSocket* socket = server_->nextPendingConnection();
        lineBuffers_.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, &DebugBridge::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &DebugBridge::onSocketDisconnected);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void DebugBridge::onReadyRead() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QByteArray& buffer = lineBuffers_[socket];
    buffer += socket->readAll();

    int newlineIdx;
    while ((newlineIdx = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newlineIdx);
        buffer.remove(0, newlineIdx + 1);
        if (!line.trimmed().isEmpty()) {
            handleLine(socket, line);
        }
    }
}

void DebugBridge::onSocketDisconnected() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    lineBuffers_.remove(socket);
}

void DebugBridge::handleLine(QTcpSocket* socket, const QByteArray& line) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject response;
        response["id"] = -1;
        response["ok"] = false;
        response["error"] = QStringLiteral("invalid JSON request: %1").arg(parseError.errorString());
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n");
        return;
    }

    const QJsonObject req = doc.object();
    const QJsonValue idValue = req.contains(QStringLiteral("id")) ? req.value(QStringLiteral("id")) : QJsonValue(-1);
    const QString cmd = req.value(QStringLiteral("cmd")).toString();

    QJsonObject response;
    if (cmd.isEmpty()) {
        response["ok"] = false;
        response["error"] = QStringLiteral("missing 'cmd'");
    } else {
        try {
            response = execute(cmd, req);

            // "id"/"ok" are about to be overwritten below, so a cmd* handler must
            // never set them. qFatal, not Q_ASSERT (inert under this build's QT_NO_DEBUG).
            if (response.contains(QStringLiteral("id")) || response.contains(QStringLiteral("ok"))) {
                qWarning() << "DebugBridge: command" << cmd
                           << "returned a reserved envelope key ('id' and/or 'ok') in its result -- "
                              "it will be silently overwritten by the response envelope; rename the "
                              "handler's field instead (e.g. entityId, materialId, tagId) -- see "
                              "the maintainer notes";
                qFatal("DebugBridge: command '%s' returned a reserved envelope key ('id'/'ok') -- aborting",
                       qUtf8Printable(cmd));
            }

            response["ok"] = true;
        } catch (const CommandError& e) {
            response = QJsonObject();
            response["ok"] = false;
            response["error"] = e.message;
        }
    }
    response["id"] = idValue;

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + "\n");
}

QJsonObject DebugBridge::execute(const QString& cmd, const QJsonObject& req) {
    if (cmd == QStringLiteral("ping")) return cmdPing();
    if (cmd == QStringLiteral("scene")) return cmdScene();
    if (cmd == QStringLiteral("camera")) return cmdCamera();
    if (cmd == QStringLiteral("camera_set")) return cmdCameraSet(req);
    if (cmd == QStringLiteral("status")) return cmdStatus();
    if (cmd == QStringLiteral("selection")) return cmdSelection();
    if (cmd == QStringLiteral("edit_context")) return cmdEditContext();
    if (cmd == QStringLiteral("entity_info")) return cmdEntityInfo();
    if (cmd == QStringLiteral("vcb")) return cmdVcb();
    if (cmd == QStringLiteral("overlay_stats")) return cmdOverlayStats();
    if (cmd == QStringLiteral("tags")) return cmdTags();
    if (cmd == QStringLiteral("pick")) return cmdPick(req);
    if (cmd == QStringLiteral("infer")) return cmdInfer(req);
    if (cmd == QStringLiteral("project")) return cmdProject(req);
    if (cmd == QStringLiteral("screenshot")) return cmdScreenshot(req);
    if (cmd == QStringLiteral("events")) return cmdEvents(req);
    if (cmd == QStringLiteral("mouse_press") || cmd == QStringLiteral("mouse_move") || cmd == QStringLiteral("mouse_release")) {
        return cmdMouse(cmd, req);
    }
    if (cmd == QStringLiteral("key")) return cmdKey(req);
    if (cmd == QStringLiteral("tool")) return cmdTool(req);
    if (cmd == QStringLiteral("menu_action")) return cmdMenuAction(req);
    if (cmd == QStringLiteral("modal")) return cmdModal(req);
    if (cmd == QStringLiteral("context_menu")) return cmdContextMenu(req);
    if (cmd == QStringLiteral("doc")) return cmdDoc(req);
    if (cmd == QStringLiteral("tag")) return cmdTag(req);
    if (cmd == QStringLiteral("material")) return cmdMaterial(req);
    if (cmd == QStringLiteral("style")) return cmdStyle(req);
    if (cmd == QStringLiteral("shadow")) return cmdShadow(req);
    if (cmd == QStringLiteral("fog")) return cmdFog(req);
    if (cmd == QStringLiteral("solid")) return cmdSolid(req);
    throw CommandError{QStringLiteral("unknown command '%1'").arg(cmd)};
}

QJsonObject DebugBridge::cmdPing() const {
    QJsonObject o;
    o["app"] = QStringLiteral("planura");
    o["protocol"] = 1;
    return o;
}

QJsonObject DebugBridge::cmdScene() const {
    QJsonArray vertsArr;
    QJsonArray edgesArr;
    QJsonArray facesArr;
    QJsonArray instancesArr;

    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (agent) {
        // TagStore may be absent from this kernel -- tagOf() falls back to
        // Untagged's id directly rather than special-casing a missing agent
        // per entity below.
        auto tagStore = kernel_.agentAs<agent::TagStore>(agent::kTagStoreName);
        const auto tagOf = [&tagStore](const events::EntityRef& ref) {
            return tagStore ? static_cast<qint64>(tagStore->tagOf(ref)) : static_cast<qint64>(agent::kUntaggedTagId);
        };

        const geo::Model& model = agent->model();

        for (const auto& [id, v] : model.vertices()) {
            const events::EntityRef ref{geo::EntityKind::Vertex, id};
            QJsonObject o;
            o["id"] = static_cast<qint64>(id);
            o["x"] = v.pos.x;
            o["y"] = v.pos.y;
            o["z"] = v.pos.z;
            o["hidden"] = agent->isHidden(ref);
            o["tag"] = tagOf(ref);
            vertsArr.append(o);
        }

        for (const auto& [id, e] : model.edges()) {
            const geo::HalfEdge* he0 = model.halfEdge(e.halfEdges[0]);
            const geo::HalfEdge* he1 = model.halfEdge(e.halfEdges[1]);
            if (!he0 || !he1) continue;
            const events::EntityRef ref{geo::EntityKind::Edge, id};
            QJsonObject o;
            o["id"] = static_cast<qint64>(id);
            o["v1"] = static_cast<qint64>(he0->origin);
            o["v2"] = static_cast<qint64>(he1->origin);
            o["hidden"] = agent->isHidden(ref);
            o["tag"] = tagOf(ref);
            edgesArr.append(o);
        }

        for (const auto& [id, f] : model.faces()) {
            QJsonArray loopArr;
            for (geo::Id vertexId : model.faceVertexLoop(id)) {
                loopArr.append(static_cast<qint64>(vertexId));
            }
            const events::EntityRef ref{geo::EntityKind::Face, id};
            QJsonObject o;
            o["id"] = static_cast<qint64>(id);
            o["loop"] = loopArr;
            o["normal"] = vec3ToJson(f.normal);
            o["hidden"] = agent->isHidden(ref);
            o["tag"] = tagOf(ref);
            facesArr.append(o);
        }

        // one level deep -- the root's direct child Instances
        // only, not a full recursive scene tree (no nested-group query need
        // has come up yet).
        const geo::Scene& scene = agent->scene();
        for (const geo::Instance& inst : scene.root().children) {
            const geo::Definition* def = scene.definition(inst.definitionId);
            QJsonObject o;
            o["id"] = static_cast<qint64>(inst.id);
            o["definitionId"] = static_cast<qint64>(inst.definitionId);
            o["name"] = QString::fromStdString(inst.name);
            o["isGroup"] = def != nullptr && def->isGroup;
            o["translation"] = vec3ToJson(inst.transform.t);
            o["edgeCount"] = def != nullptr ? static_cast<qint64>(def->model.edges().size()) : 0;
            o["faceCount"] = def != nullptr ? static_cast<qint64>(def->model.faces().size()) : 0;
            instancesArr.append(o);
        }
    }

    QJsonObject result;
    result["vertices"] = vertsArr;
    result["edges"] = edgesArr;
    result["faces"] = facesArr;
    result["instances"] = instancesArr;
    return result;
}

QJsonObject DebugBridge::cmdCamera() const {
    viewport::ViewportWidget* vp = window_->viewportWidget();
    const viewport::Camera& cam = vp->camera();

    QJsonObject targetJson;
    const QVector3D target = cam.target();
    targetJson["x"] = target.x();
    targetJson["y"] = target.y();
    targetJson["z"] = target.z();

    QJsonObject o;
    o["azimuthDeg"] = cam.azimuthDeg();
    o["elevationDeg"] = cam.elevationDeg();
    o["distance"] = cam.distance();
    o["fovYDeg"] = cam.fovYDeg();
    o["target"] = targetJson;
    o["viewportW"] = vp->width();
    o["viewportH"] = vp->height();
    return o;
}

// Via CameraStore::restoreCamera -- the agent's own notifying entry point,
// so the viewport re-pulls exactly like a file load.
QJsonObject DebugBridge::cmdCameraSet(const QJsonObject& req) {
    auto camera = kernel_.agentAs<agent::CameraStore>(agent::kCameraStoreName);
    if (!camera) {
        throw CommandError{QStringLiteral("CameraStore not registered")};
    }

    agent::CameraState state = camera->state();
    if (req.value("azimuthDeg").isDouble()) state.azimuthDeg = req.value("azimuthDeg").toDouble();
    if (req.value("elevationDeg").isDouble()) state.elevationDeg = req.value("elevationDeg").toDouble();
    if (req.value("distance").isDouble()) state.distance = req.value("distance").toDouble();
    if (req.value("fovYDeg").isDouble()) state.fovYDeg = req.value("fovYDeg").toDouble();
    if (req.value("target").isObject()) {
        const QJsonObject t = req.value("target").toObject();
        if (t.value("x").isDouble()) state.target.x = t.value("x").toDouble();
        if (t.value("y").isDouble()) state.target.y = t.value("y").toDouble();
        if (t.value("z").isDouble()) state.target.z = t.value("z").toDouble();
    }

    camera->restoreCamera(state);
    QCoreApplication::processEvents();  // let the viewport's re-pull repaint settle, same as cmdFog's toggles
    return cmdCamera();
}

QJsonObject DebugBridge::cmdStatus() const {
    QJsonObject o;
    o["hint"] = window_->statusHintText();
    o["tool"] = toolIdToString(window_->activeTool());
    return o;
}

QJsonObject DebugBridge::cmdSelection() const {
    QJsonArray itemsArr;

    auto agent = kernel_.agentAs<agent::SelectionStore>(agent::kSelectionStoreName);
    if (agent) {
        for (const events::EntityRef& ref : agent->items()) {
            QJsonObject o;
            o["kind"] = entityKindToString(ref.kind);
            o["id"] = static_cast<qint64>(ref.id);
            itemsArr.append(o);
        }
    }

    QJsonObject result;
    result["items"] = itemsArr;
    return result;
}

QJsonObject DebugBridge::cmdEditContext() const {
    QJsonArray pathArr;
    bool atRoot = true;

    auto agent = kernel_.agentAs<agent::EditContextStore>(agent::kEditContextStoreName);
    if (agent) {
        for (geo::Id id : agent->path()) {
            pathArr.append(static_cast<qint64>(id));
        }
        atRoot = agent->atRoot();
    }

    QJsonObject result;
    result["path"] = pathArr;
    result["atRoot"] = atRoot;
    return result;
}

QJsonObject DebugBridge::cmdEntityInfo() const {
    QJsonObject o;
    o["text"] = window_->entityInfoText();
    return o;
}

QJsonObject DebugBridge::cmdVcb() const {
    QJsonObject o;
    o["label"] = window_->vcbLabelText();
    o["text"] = window_->vcbValueText();
    return o;
}

QJsonObject DebugBridge::cmdOverlayStats() const {
    const viewport::ViewportWidget::OverlayStats s = window_->viewportWidget()->overlayStats();
    QJsonObject o;
    o["selectionEdgeVertexCount"] = s.selectionEdgeVertexCount;
    o["selectionFaceVertexCount"] = s.selectionFaceVertexCount;
    o["selectionPointVertexCount"] = s.selectionPointVertexCount;
    o["pendingSelectionEdgeFloats"] = s.pendingSelectionEdgeFloats;
    o["pendingSelectionFaceFloats"] = s.pendingSelectionFaceFloats;
    o["pendingSelectionPointFloats"] = s.pendingSelectionPointFloats;
    o["selectionDirty"] = s.selectionDirty;
    o["screenRectSet"] = s.screenRectSet;
    o["paintCount"] = s.paintCount;
    o["lastPaintSelectionEdgeVertexCount"] = s.lastPaintSelectionEdgeVertexCount;
    QJsonArray firstVerts;
    for (float v : s.firstSelectionEdgeVerts) firstVerts.append(v);
    o["firstSelectionEdgeVerts"] = firstVerts;
    return o;
}

namespace {

// Appends one {kind,entityId,tagId} per entity explicitly off Untagged;
// shared by cmdTags()'s three passes.
template <typename Container>
void appendTagAssignments(QJsonArray& out, const Container& container, geo::EntityKind kind,
                           const agent::TagStore& tagStore) {
    for (const auto& [id, entity] : container) {
        (void)entity;
        const events::EntityRef ref{kind, id};
        const std::uint64_t tagId = tagStore.tagOf(ref);
        if (tagId == agent::kUntaggedTagId) continue;  // implicit -- not an explicit assignment
        QJsonObject o;
        o["kind"] = entityKindToString(kind);
        o["entityId"] = static_cast<qint64>(id);
        o["tagId"] = static_cast<qint64>(tagId);
        out.append(o);
    }
}

}  // namespace

QJsonObject DebugBridge::cmdTags() const {
    QJsonArray tagsArr;
    QJsonArray assignmentsArr;

    auto tagStore = kernel_.agentAs<agent::TagStore>(agent::kTagStoreName);
    if (tagStore) {
        for (const agent::Tag& tag : tagStore->tags()) {
            QJsonObject o;
            o["id"] = static_cast<qint64>(tag.id);
            o["name"] = QString::fromStdString(tag.name);
            o["visible"] = tag.visible;
            tagsArr.append(o);
        }

        auto geometry = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
        if (geometry) {
            const geo::Model& model = geometry->model();
            appendTagAssignments(assignmentsArr, model.vertices(), geo::EntityKind::Vertex, *tagStore);
            appendTagAssignments(assignmentsArr, model.edges(), geo::EntityKind::Edge, *tagStore);
            appendTagAssignments(assignmentsArr, model.faces(), geo::EntityKind::Face, *tagStore);
        }
    }

    QJsonObject result;
    result["tags"] = tagsArr;
    result["assignments"] = assignmentsArr;
    return result;
}

QJsonObject DebugBridge::cmdPick(const QJsonObject& req) const {
    const double x = requireNumber(req, "x");
    const double y = requireNumber(req, "y");

    viewport::ViewportWidget* vp = window_->viewportWidget();
    const geo::Ray ray = vp->makeRay(QPointF(x, y));
    geo::PickOptions opts = vp->tolerancesAt(vp->camera().distance());

    geo::ScenePickResult result;
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (agent) {
        // Hidden entities are pick-through, matching ToolController::pick's
        // real-input path. pickInstances=true so grouped/componentized
        // geometry resolves to its containing instance instead of "none".
        auto tagStore = kernel_.agentAs<agent::TagStore>(agent::kTagStoreName);
        opts.filter = [agent, tagStore](geo::EntityKind kind, geo::Id id) {
            if (agent->isHidden({kind, id})) return false;
            if (tagStore && !tagStore->isEntityVisible({kind, id})) return false;
            return true;
        };
        // Scoped to the current editing context, matching ToolController::pickScene.
        auto editContextStore = kernel_.agentAs<agent::EditContextStore>(agent::kEditContextStoreName);
        const std::vector<geo::Id> contextPath = editContextStore ? editContextStore->path() : std::vector<geo::Id>{};
        result = geo::pickScene(agent->scene(), ray, opts, /*pickInstances=*/true, contextPath);
        if (result.instanceId != geo::kInvalidId) {
            // pickScene's filter can't see instance-level hidden/tag state, so a
            // hidden/tag-invisible top-level instance is post-filtered here.
            const events::EntityRef instRef{geo::EntityKind::Instance, result.instanceId};
            if (agent->isHidden(instRef) || (tagStore && !tagStore->isEntityVisible(instRef))) {
                result = geo::ScenePickResult{};
            }
        }
    }

    QJsonObject o;
    o["kind"] = pickKindToString(result.kind);
    // entityId, not "id" (handleLine() overwrites a bare "id" with the correlation id).
    o["entityId"] = static_cast<qint64>(result.id);
    o["point"] = vec3ToJson(result.point);
    o["depth"] = result.depth;
    // 0 (geo::kInvalidId) when the hit isn't inside any instance -- a root
    // hit, or nothing at all (kind == "none").
    o["instanceId"] = static_cast<qint64>(result.instanceId);
    return o;
}

QJsonObject DebugBridge::cmdInfer(const QJsonObject& req) const {
    const double x = requireNumber(req, "x");
    const double y = requireNumber(req, "y");

    viewport::ViewportWidget* vp = window_->viewportWidget();
    const geo::Ray ray = vp->makeRay(QPointF(x, y));

    geo::InferenceContext ctx;
    ctx.tols = vp->tolerancesAt(vp->camera().distance());

    geo::Inference inf;
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (agent) {
        inf = geo::infer(agent->model(), ray, ctx);
    }

    QJsonObject o;
    o["kind"] = inferenceKindToString(inf.kind);
    o["pos"] = vec3ToJson(inf.pos);
    o["refId"] = static_cast<qint64>(inf.refId);
    return o;
}

QJsonObject DebugBridge::cmdProject(const QJsonObject& req) const {
    const double x = requireNumber(req, "x");
    const double y = requireNumber(req, "y");
    const double z = requireNumber(req, "z");

    viewport::ViewportWidget* vp = window_->viewportWidget();
    const viewport::Camera& cam = vp->camera();
    const int w = vp->width();
    const int h = vp->height();
    const float aspect = h > 0 ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
    const QMatrix4x4 v1 = cam.projectionMatrix(aspect) * cam.viewMatrix();

    const QVector4D clip = v1 * QVector4D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), 1.0f);

    double px = 0.0;
    double py = 0.0;
    bool visible = false;
    if (std::abs(clip.w()) > 1e-9f) {
        const float ndcX = clip.x() / clip.w();
        const float ndcY = clip.y() / clip.w();
        const float ndcZ = clip.z() / clip.w();
        px = (ndcX * 0.5f + 0.5f) * static_cast<float>(w);
        py = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(h);  // Qt pixel space: y flipped, top-left origin
        // In front of the camera AND inside the viewport on every axis --
        // a point beyond the window's edge must report visible=false, or a
        // scenario can "click" coordinates no real user could reach.
        visible = clip.w() > 0.0f && ndcZ >= -1.0f && ndcZ <= 1.0f && ndcX >= -1.0f && ndcX <= 1.0f &&
                  ndcY >= -1.0f && ndcY <= 1.0f;
    }

    QJsonObject o;
    o["x"] = px;
    o["y"] = py;
    o["visible"] = visible;
    return o;
}

QJsonObject DebugBridge::cmdScreenshot(const QJsonObject& req) const {
    const QString path = requireString(req, "path");
    const QImage image = window_->viewportWidget()->grabFramebuffer();
    const bool saved = image.save(path);
    QJsonObject o;
    o["saved"] = saved;
    return o;
}

QJsonObject DebugBridge::cmdEvents(const QJsonObject& req) const {
    const QJsonValue sinceValue = req.value(QStringLiteral("since"));
    const int since = sinceValue.isDouble() ? sinceValue.toInt() : 0;

    QJsonArray eventsArr;
    for (const TraceEntry& entry : traceLog_) {
        if (entry.seq <= since) continue;
        QJsonObject o;
        o["seq"] = entry.seq;
        o["name"] = QString::fromStdString(entry.name);
        o["subscribers"] = static_cast<qint64>(entry.subscribers);
        o["tMs"] = entry.tMs;
        eventsArr.append(o);
    }

    QJsonObject result;
    result["events"] = eventsArr;
    result["nextSince"] = nextTraceSeq_ - 1;  // last seq handed out overall, 0 if none yet
    return result;
}

void DebugBridge::onDispatchTrace(const ordo::core::DispatchRecord& record) {
    TraceEntry entry;
    entry.seq = nextTraceSeq_++;
    entry.name = traceRecordName(record);
    entry.subscribers = record.subscriberCount;
    entry.tMs = traceClock_.elapsed();

    traceLog_.push_back(std::move(entry));
    if (traceLog_.size() > static_cast<std::size_t>(kTraceLogCapacity)) {
        traceLog_.pop_front();
    }
}

QJsonObject DebugBridge::cmdMouse(const QString& cmd, const QJsonObject& req) {
    const double x = requireNumber(req, "x");
    const double y = requireNumber(req, "y");

    viewport::ViewportWidget* vp = window_->viewportWidget();
    const QPointF localPos(x, y);
    const QPointF globalPos = vp->mapToGlobal(localPos.toPoint());
    const Qt::KeyboardModifiers mods = modifiersFromJson(req.value(QStringLiteral("modifiers")).toArray());

    QEvent::Type type = QEvent::MouseMove;
    Qt::MouseButton button = Qt::NoButton;
    if (cmd == QStringLiteral("mouse_press")) {
        type = QEvent::MouseButtonPress;
        button = buttonFromString(req.value(QStringLiteral("button")).toString(QStringLiteral("left")));
        pressedButtons_ |= button;  // Qt convention: buttons() includes the just-pressed button
    } else if (cmd == QStringLiteral("mouse_release")) {
        type = QEvent::MouseButtonRelease;
        button = buttonFromString(req.value(QStringLiteral("button")).toString(QStringLiteral("left")));
        pressedButtons_ &= ~button;  // Qt convention: buttons() excludes the just-released button
    }

    // postEvent + processEvents (not sendEvent) so delivery matches real input
    // exactly; the reply waits for the resulting dispatch to finish.
    QCoreApplication::postEvent(vp, new QMouseEvent(type, localPos, globalPos, button, pressedButtons_, mods));
    QCoreApplication::processEvents();

    QJsonObject o;
    o["posted"] = true;
    return o;
}

QJsonObject DebugBridge::cmdKey(const QJsonObject& req) {
    const QString keyStr = requireString(req, "key");
    const Qt::KeyboardModifiers extraMods = modifiersFromJson(req.value(QStringLiteral("modifiers")).toArray());

    int key = 0;
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (const std::optional<BareModifierKey> bare = bareModifierKeyFromString(keyStr)) {
        // A standalone modifier press (e.g. `bridge.key("Ctrl")` for Flip's
        // copy-mode toggle) -- bypasses QKeySequence entirely.
        key = bare->key;
        mods = bare->modifier | extraMods;
    } else {
        const QKeySequence seq(keyStr);
        if (seq.isEmpty()) {
            throw CommandError{QStringLiteral("unrecognized key '%1'").arg(keyStr)};
        }
        const QKeyCombination combo = seq[0];
        key = combo.key();
        mods = combo.keyboardModifiers() | extraMods;
    }

    viewport::ViewportWidget* vp = window_->viewportWidget();
    QCoreApplication::postEvent(vp, new QKeyEvent(QEvent::KeyPress, key, mods));
    QCoreApplication::postEvent(vp, new QKeyEvent(QEvent::KeyRelease, key, mods));
    QCoreApplication::processEvents();

    QJsonObject o;
    o["posted"] = true;
    return o;
}

QJsonObject DebugBridge::cmdTool(const QJsonObject& req) {
    const QString name = requireString(req, "name");
    bool known = false;
    const events::ToolId tool = toolIdFromString(name, known);
    if (!known) {
        throw CommandError{QStringLiteral("unknown tool '%1'").arg(name)};
    }

    QAction* action = window_->toolAction(tool);
    if (!action) {
        throw CommandError{QStringLiteral("no toolbar action for tool '%1'").arg(name)};
    }
    action->trigger();
    QCoreApplication::processEvents();

    QJsonObject o;
    o["tool"] = toolIdToString(window_->activeTool());
    return o;
}

QJsonObject DebugBridge::cmdMenuAction(const QJsonObject& req) {
    const QString menuTitle = requireString(req, "menu");
    const QString text = requireString(req, "text");

    QMenu* targetMenu = nullptr;
    for (QAction* menuAction : window_->menuBar()->actions()) {
        QMenu* menu = menuAction->menu();
        if (menu && stripMnemonic(menu->title()) == menuTitle) {
            targetMenu = menu;
            break;
        }
    }
    if (!targetMenu) {
        throw CommandError{QStringLiteral("no menu titled '%1'").arg(menuTitle)};
    }

    // Searches recursively into submenus (Standard Views, Solid Tools,
    // Export, ... are QMenus nested inside a top-level menu). First text
    // match wins, depth-first in menu order.
    const std::function<QAction*(QMenu*)> findAction = [&](QMenu* menu) -> QAction* {
        for (QAction* action : menu->actions()) {
            if (QMenu* sub = action->menu()) {
                if (QAction* found = findAction(sub)) return found;
                continue;
            }
            if (stripMnemonic(action->text()) == text) {
                return action;
            }
        }
        return nullptr;
    };
    QAction* target = findAction(targetMenu);
    if (!target) {
        throw CommandError{QStringLiteral("no action '%1' in menu '%2'").arg(text, menuTitle)};
    }

    target->trigger();
    QCoreApplication::processEvents();

    QJsonObject o;
    o["triggered"] = true;
    return o;
}

QJsonObject DebugBridge::cmdModal(const QJsonObject& req) {
    // ToolController is a Presenter, not an Agent -- reached via MainWindow's
    // one accessor instead of kernel_.agentAs<>().
    tools::ToolController* controller = window_->toolController();

    const QString action = requireString(req, "action");

    if (action == QStringLiteral("queue")) {
        const QString kindStr = requireString(req, "kind");
        bool known = false;
        const tools::ToolController::ScriptedModalAnswer::Kind kind = modalKindFromString(kindStr, known);
        if (!known) {
            throw CommandError{QStringLiteral("unknown modal kind '%1'").arg(kindStr)};
        }

        tools::ToolController::ScriptedModalAnswer answer;
        answer.kind = kind;
        // accepted default true; text default "" (unused by warning/a declined prompt).
        answer.accepted = req.value(QStringLiteral("accepted")).toBool(true);
        answer.text = req.value(QStringLiteral("text")).toString().toStdString();

        if (controller) controller->queueModalAnswer(std::move(answer));

        QJsonObject o;
        o["queued"] = true;
        return o;
    }

    if (action == QStringLiteral("status")) {
        QJsonObject o;
        o["queueLength"] = controller ? static_cast<qint64>(controller->pendingModalAnswers()) : 0;

        const std::optional<tools::ToolController::LastModal> last =
            controller ? controller->lastModal() : std::nullopt;
        if (last) {
            QJsonObject lm;
            lm["kind"] = modalKindToString(last->kind);
            lm["message"] = QString::fromStdString(last->message);
            o["lastModal"] = lm;
        } else {
            o["lastModal"] = QJsonValue();  // null -- no modal opened yet this run
        }
        return o;
    }

    throw CommandError{QStringLiteral("unknown modal action '%1'").arg(action)};
}

QJsonObject DebugBridge::cmdContextMenu(const QJsonObject& req) {
    // Same "reaches past MainWindow into ToolController specifically"
    // exception cmdModal's own comment documents.
    tools::ToolController* controller = window_->toolController();

    const QString action = requireString(req, "action");
    const double x = requireNumber(req, "x");
    const double y = requireNumber(req, "y");

    // buildContextMenuItems() is the exact same pure-function call
    // ToolController::onViewportContextMenu makes for a real right-click.
    // No QMenu is ever constructed here, unlike that interactive path.
    const std::vector<ui::ContextMenuItem> items =
        controller ? controller->buildContextMenuItems(QPointF(x, y)) : std::vector<ui::ContextMenuItem>{};

    if (action == QStringLiteral("list")) {
        QJsonArray itemsArr;
        for (const ui::ContextMenuItem& item : items) {
            QJsonObject o;
            o["label"] = QString::fromStdString(item.label);
            o["checkable"] = item.checkable;
            o["checked"] = item.checked;
            itemsArr.append(o);
        }
        QJsonObject result;
        result["items"] = itemsArr;
        return result;
    }

    if (action == QStringLiteral("trigger")) {
        const QString label = requireString(req, "label");
        bool triggered = false;
        for (const ui::ContextMenuItem& item : items) {
            if (QString::fromStdString(item.label) == label) {
                if (item.action) item.action();
                triggered = true;
                break;
            }
        }
        QJsonObject o;
        o["triggered"] = triggered;
        return o;
    }

    throw CommandError{QStringLiteral("unknown context_menu action '%1'").arg(action)};
}

QJsonObject DebugBridge::docStateJson() const {
    auto document = kernel_.agentAs<agent::DocumentStore>(agent::kDocumentStoreName);

    QJsonObject o;
    o["filePath"] = document ? QString::fromStdString(document->filePath()) : QString();
    o["dirty"] = document && document->dirty();
    o["units"] = document ? QString::fromStdString(document->units()) : QString();

    // windowTitle() returns the raw "[*]" template -- Qt only resolves that
    // placeholder on the native caption, not this accessor. Resolved here the
    // same way ("*" while dirty) to match what the OS window manager shows.
    QString title = window_->windowTitle();
    title.replace(QStringLiteral("[*]"), (document && document->dirty()) ? QStringLiteral("*") : QString());
    o["windowTitle"] = title;

    return o;
}

QJsonObject DebugBridge::styleStateJson() const {
    auto style = kernel_.agentAs<agent::StyleStore>(agent::kStyleStoreName);

    QJsonObject o;
    o["faceStyle"] = faceStyleToString(style ? style->faceStyle() : events::FaceStyle::ShadedWithTextures);
    o["profiles"] = style && style->profiles();
    o["depthCue"] = style && style->depthCue();
    o["backEdges"] = style && style->backEdges();
    o["ambientOcclusion"] = style && style->ambientOcclusion();
    o["aoStrength"] = style ? style->aoStrength() : agent::kDefaultAoStrength;
    if (style) {
        o["frontColor"] = QJsonArray{style->defaultFrontColor().r, style->defaultFrontColor().g, style->defaultFrontColor().b};
        o["backColor"] = QJsonArray{style->defaultBackColor().r, style->defaultBackColor().g, style->defaultBackColor().b};
    } else {
        o["frontColor"] = QJsonArray{agent::kDefaultFrontColorR, agent::kDefaultFrontColorG, agent::kDefaultFrontColorB};
        o["backColor"] = QJsonArray{agent::kDefaultBackColorR, agent::kDefaultBackColorG, agent::kDefaultBackColorB};
    }
    return o;
}

QJsonObject DebugBridge::shadowStateJson() const {
    auto shadow = kernel_.agentAs<agent::ShadowStore>(agent::kShadowStoreName);

    QJsonObject o;
    o["useSunForShading"] = shadow && shadow->useSunForShading();
    o["showShadows"] = shadow && shadow->showShadows();
    o["latitudeDeg"] = shadow ? shadow->latitudeDeg() : agent::kDefaultLatitudeDeg;
    o["longitudeDeg"] = shadow ? shadow->longitudeDeg() : agent::kDefaultLongitudeDeg;
    o["month"] = shadow ? shadow->month() : agent::kDefaultMonth;
    o["day"] = shadow ? shadow->day() : agent::kDefaultDay;
    o["hourLocal"] = shadow ? shadow->hourLocal() : agent::kDefaultHourLocal;
    o["light"] = shadow ? shadow->light() : agent::kDefaultLight;
    o["dark"] = shadow ? shadow->dark() : agent::kDefaultDark;
    return o;
}

QJsonObject DebugBridge::fogStateJson() const {
    auto fog = kernel_.agentAs<agent::FogStore>(agent::kFogStoreName);

    QJsonObject o;
    o["enabled"] = fog && fog->enabled();
    o["startDistance"] = fog ? fog->startDistance() : agent::kDefaultFogStartDistance;
    o["endDistance"] = fog ? fog->endDistance() : agent::kDefaultFogEndDistance;
    o["useBackgroundColor"] = !fog || fog->useBackgroundColor();
    if (fog) {
        o["color"] = QJsonArray{fog->colorR(), fog->colorG(), fog->colorB()};
    } else {
        o["color"] = QJsonArray{0.5, 0.5, 0.5};
    }
    return o;
}

QJsonObject DebugBridge::cmdDoc(const QJsonObject& req) {
    const QString action = requireString(req, "action");

    if (action == QStringLiteral("state")) {
        return docStateJson();
    }
    if (action == QStringLiteral("new")) {
        kernel_.send(events::NewDocumentRequested{});
        return docStateJson();
    }
    if (action == QStringLiteral("open")) {
        const QString path = requireString(req, "path");
        kernel_.send(events::OpenDocumentRequested{path.toStdString()});
        return docStateJson();
    }
    if (action == QStringLiteral("save")) {
        const QString path = requireString(req, "path");
        kernel_.send(events::SaveDocumentRequested{path.toStdString()});
        return docStateJson();
    }
    // Same bypass as new/open/save -- skips the Import/Export OBJ QFileDialogs.
    // Returns doc state unchanged by export; check scene()/the file on disk
    // to confirm the actual effect.
    if (action == QStringLiteral("export_obj")) {
        const QString path = requireString(req, "path");
        kernel_.send(events::ExportObjRequested{path.toStdString()});
        return docStateJson();
    }
    if (action == QStringLiteral("import_obj")) {
        const QString path = requireString(req, "path");
        kernel_.send(events::ImportObjRequested{path.toStdString()});
        return docStateJson();
    }

    throw CommandError{QStringLiteral("unknown doc action '%1'").arg(action)};
}

QJsonObject DebugBridge::cmdTag(const QJsonObject& req) {
    const QString action = requireString(req, "action");

    if (action == QStringLiteral("create")) {
        const QString name = req.value(QStringLiteral("name")).toString();
        kernel_.send(events::TagCreateRequested{name.toStdString()});
        QJsonObject o;
        o["created"] = true;
        return o;
    }

    if (action == QStringLiteral("assign")) {
        const QString kindStr = requireString(req, "kind");
        const double idNum = requireNumber(req, "entityId");
        const double tagIdNum = requireNumber(req, "tagId");

        bool knownKind = false;
        const geo::EntityKind kind = entityKindFromString(kindStr, knownKind);
        if (!knownKind) {
            throw CommandError{QStringLiteral("unknown entity kind '%1'").arg(kindStr)};
        }

        const events::EntityRef ref{kind, static_cast<geo::Id>(idNum)};
        kernel_.send(events::TagAssignRequested{{ref}, static_cast<std::uint64_t>(tagIdNum)});
        QJsonObject o;
        o["assigned"] = true;
        return o;
    }

    throw CommandError{QStringLiteral("unknown tag action '%1'").arg(action)};
}

QJsonObject DebugBridge::cmdMaterial(const QJsonObject& req) {
    const QString action = requireString(req, "action");

    if (action == QStringLiteral("create")) {
        const QString name = req.value(QStringLiteral("name")).toString();
        const double r = requireNumber(req, "r");
        const double g = requireNumber(req, "g");
        const double b = requireNumber(req, "b");
        const double opacity = req.contains(QStringLiteral("opacity")) ? requireNumber(req, "opacity") : 1.0;
        kernel_.send(events::MaterialCreateRequested{name.toStdString(), r, g, b, opacity});

        // create() is synchronous and always appends, so the new material is
        // materials().back() right after send(). materialId, not "id".
        auto materials = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
        QJsonObject o;
        o["materialId"] = (materials && !materials->materials().empty())
                               ? static_cast<qint64>(materials->materials().back().id)
                               : 0;
        return o;
    }

    if (action == QStringLiteral("paint")) {
        const QString kindStr = requireString(req, "kind");
        // entityId, not "id"
        const double idNum = requireNumber(req, "entityId");
        const double materialIdNum = requireNumber(req, "materialId");

        bool knownKind = false;
        const geo::EntityKind kind = entityKindFromString(kindStr, knownKind);
        if (!knownKind) {
            throw CommandError{QStringLiteral("unknown entity kind '%1'").arg(kindStr)};
        }

        const events::EntityRef ref{kind, static_cast<geo::Id>(idNum)};
        kernel_.send(events::PaintRequested{{ref}, static_cast<geo::Id>(materialIdNum)});
        QJsonObject o;
        o["painted"] = true;
        return o;
    }

    if (action == QStringLiteral("set_active")) {
        // materialId, not "id"
        const double idNum = requireNumber(req, "materialId");
        kernel_.send(events::SetActiveMaterialRequested{static_cast<geo::Id>(idNum)});

        // setActive() is unconditional (accepts any id verbatim) -- this
        // always echoes back exactly what was sent.
        auto materials = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
        QJsonObject o;
        o["materialId"] = materials ? static_cast<qint64>(materials->activeMaterialId()) : 0;
        return o;
    }

    if (action == QStringLiteral("sample")) {
        // Read-only. Real gesture: tool("PaintBucket") + mouse_press{modifiers:["alt"]}.
        const QString kindStr = requireString(req, "kind");
        const double idNum = requireNumber(req, "entityId");

        bool knownKind = false;
        const geo::EntityKind kind = entityKindFromString(kindStr, knownKind);
        if (!knownKind) {
            throw CommandError{QStringLiteral("unknown entity kind '%1'").arg(kindStr)};
        }

        auto materials = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
        geo::Id materialId = 0;
        if (materials) {
            const agent::MaterialAssignment* assignment =
                materials->assignment(events::EntityRef{kind, static_cast<geo::Id>(idNum)});
            materialId = assignment ? assignment->frontMaterialId : geo::Id{0};
        }
        QJsonObject o;
        o["materialId"] = static_cast<qint64>(materialId);
        return o;
    }

    if (action == QStringLiteral("set_texture")) {
        // "materialId", not "id" (envelope-reserved-key collision).
        const double idNum = requireNumber(req, "materialId");
        // Absent -> empty QString, read by material_texture_commands.cpp as "clear".
        const QString path = req.value(QStringLiteral("path")).toString();
        const double tileW = req.contains(QStringLiteral("tileW")) ? requireNumber(req, "tileW") : 1.0;
        const double tileH = req.contains(QStringLiteral("tileH")) ? requireNumber(req, "tileH") : 1.0;

        const geo::Id materialId = static_cast<geo::Id>(idNum);
        kernel_.send(events::MaterialSetTextureRequested{materialId, path.toStdString(), tileW, tileH});

        // Echoes the resulting texture state; an unknown materialId is a silent no-op.
        auto materials = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
        const agent::Material* m = materials ? materials->material(materialId) : nullptr;
        QJsonObject o;
        o["materialId"] = static_cast<qint64>(materialId);
        o["textured"] = m != nullptr && !m->assetHash.empty();
        return o;
    }

    if (action == QStringLiteral("set_uv_transform")) {
        // "kind"/"entityId", not "id" (envelope-reserved-key collision).
        const QString kindStr = requireString(req, "kind");
        const double idNum = requireNumber(req, "entityId");
        bool knownKind = false;
        const geo::EntityKind kind = entityKindFromString(kindStr, knownKind);
        if (!knownKind) {
            throw CommandError{QStringLiteral("unknown entity kind '%1'").arg(kindStr)};
        }

        // Each field defaults to its identity value when omitted.
        const double du = req.contains(QStringLiteral("du")) ? requireNumber(req, "du") : 0.0;
        const double dv = req.contains(QStringLiteral("dv")) ? requireNumber(req, "dv") : 0.0;
        const double rot = req.contains(QStringLiteral("rot")) ? requireNumber(req, "rot") : 0.0;
        const double su = req.contains(QStringLiteral("su")) ? requireNumber(req, "su") : 1.0;
        const double sv = req.contains(QStringLiteral("sv")) ? requireNumber(req, "sv") : 1.0;

        const events::EntityRef ref{kind, static_cast<geo::Id>(idNum)};
        kernel_.send(events::SetUvTransformRequested{ref, events::UvTransform{du, dv, rot, su, sv}});

        // Echoes the resulting transform back. An unknown/unassigned ref is a
        // silent no-op, reporting the identity default.
        auto materials = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
        const agent::MaterialAssignment* assign = materials ? materials->assignment(ref) : nullptr;
        QJsonObject o;
        o["du"] = assign ? assign->uvTransform.offsetU : 0.0;
        o["dv"] = assign ? assign->uvTransform.offsetV : 0.0;
        o["rot"] = assign ? assign->uvTransform.rotationRad : 0.0;
        o["su"] = assign ? assign->uvTransform.scaleU : 1.0;
        o["sv"] = assign ? assign->uvTransform.scaleV : 1.0;
        return o;
    }

    if (action == QStringLiteral("list")) {
        QJsonArray materialsArr;
        QJsonArray assignmentsArr;
        geo::Id activeId = 0;

        auto materials = kernel_.agentAs<agent::MaterialRepository>(agent::kMaterialRepositoryName);
        if (materials) {
            activeId = materials->activeMaterialId();
            for (const agent::Material& m : materials->materials()) {
                QJsonObject o;
                o["id"] = static_cast<qint64>(m.id);
                o["name"] = QString::fromStdString(m.name);
                QJsonArray rgba{m.r, m.g, m.b, m.opacity};
                o["rgba"] = rgba;
                o["opacity"] = m.opacity;
                // assetHash empty means untextured.
                o["assetHash"] = QString::fromStdString(m.assetHash);
                o["tileW"] = m.tileW;
                o["tileH"] = m.tileH;
                materialsArr.append(o);
            }
            for (const auto& [ref, assignment] : materials->assignments()) {
                QJsonArray tuple{entityKindToString(ref.kind), static_cast<qint64>(ref.id),
                                 static_cast<qint64>(assignment.frontMaterialId),
                                 static_cast<qint64>(assignment.backMaterialId)};
                // Mirrors plr_writer.cpp's materialAssignments row shape -- 5th
                // element present only when non-identity.
                if (!events::isIdentityUvTransform(assignment.uvTransform)) {
                    QJsonObject transform;
                    transform["du"] = assignment.uvTransform.offsetU;
                    transform["dv"] = assignment.uvTransform.offsetV;
                    transform["rot"] = assignment.uvTransform.rotationRad;
                    transform["su"] = assignment.uvTransform.scaleU;
                    transform["sv"] = assignment.uvTransform.scaleV;
                    tuple.append(transform);
                }
                assignmentsArr.append(tuple);
            }
        }

        QJsonObject o;
        o["materials"] = materialsArr;
        o["assignments"] = assignmentsArr;
        o["activeId"] = static_cast<qint64>(activeId);
        return o;
    }

    throw CommandError{QStringLiteral("unknown material action '%1'").arg(action)};
}

QJsonObject DebugBridge::cmdStyle(const QJsonObject& req) {
    const QString action = requireString(req, "action");

    if (action == QStringLiteral("set_face_style")) {
        const QString styleStr = requireString(req, "style");
        bool known = false;
        const events::FaceStyle style = faceStyleFromString(styleStr, known);
        if (!known) {
            throw CommandError{QStringLiteral("unknown face style '%1'").arg(styleStr)};
        }
        QAction* action_ = window_->faceStyleAction(style);
        if (!action_) {
            throw CommandError{QStringLiteral("no menu action for face style '%1'").arg(styleStr)};
        }
        // An exclusive QActionGroup's trigger() on an already-checked action is a no-op.
        action_->trigger();
        QCoreApplication::processEvents();
        return styleStateJson();
    }

    if (action == QStringLiteral("set_edge_flag")) {
        const QString flagStr = requireString(req, "flag");
        const bool value = requireBool(req, "value");
        bool known = false;
        const events::EdgeFlag flag = edgeFlagFromString(flagStr, known);
        if (!known) {
            throw CommandError{QStringLiteral("unknown edge flag '%1'").arg(flagStr)};
        }
        QAction* action_ = window_->edgeFlagAction(flag);
        if (!action_) {
            throw CommandError{QStringLiteral("no menu action for edge flag '%1'").arg(flagStr)};
        }
        // A checkable QAction's trigger() FLIPS its state -- only trigger on
        // a mismatch, or an already-correct value would flip away.
        if (action_->isChecked() != value) {
            action_->trigger();
            QCoreApplication::processEvents();
        }
        return styleStateJson();
    }

    if (action == QStringLiteral("set_ambient_occlusion")) {
        const bool value = requireBool(req, "ambientOcclusion");
        QAction* action_ = window_->ambientOcclusionAction();
        if (!action_) {
            throw CommandError{QStringLiteral("no menu action for ambient occlusion")};
        }
        if (action_->isChecked() != value) {
            action_->trigger();
            QCoreApplication::processEvents();
        }
        return styleStateJson();
    }

    if (action == QStringLiteral("set_ao_strength")) {
        // No UI control exists yet.
        const double aoStrength = requireNumber(req, "aoStrength");
        kernel_.send(events::SetAoStrengthRequested{aoStrength});
        return styleStateJson();
    }

    if (action == QStringLiteral("state")) {
        return styleStateJson();
    }

    throw CommandError{QStringLiteral("unknown style action '%1'").arg(action)};
}

QJsonObject DebugBridge::cmdShadow(const QJsonObject& req) {
    const QString action = requireString(req, "action");

    if (action == QStringLiteral("set_show_shadows")) {
        const bool value = requireBool(req, "showShadows");
        QAction* action_ = window_->shadowsAction();
        if (!action_) {
            throw CommandError{QStringLiteral("no menu action for shadows")};
        }
        if (action_->isChecked() != value) {
            action_->trigger();
            QCoreApplication::processEvents();
        }
        return shadowStateJson();
    }

    if (action == QStringLiteral("set_use_sun_for_shading")) {
        const bool value = requireBool(req, "useSunForShading");
        QAction* action_ = window_->useSunForShadingAction();
        if (!action_) {
            throw CommandError{QStringLiteral("no menu action for use sun for shading")};
        }
        if (action_->isChecked() != value) {
            action_->trigger();
            QCoreApplication::processEvents();
        }
        return shadowStateJson();
    }

    if (action == QStringLiteral("set_position")) {
        // No UI control exists yet.
        const double latitudeDeg = requireNumber(req, "latitudeDeg");
        const double longitudeDeg = requireNumber(req, "longitudeDeg");
        kernel_.send(events::SetSunPositionRequested{latitudeDeg, longitudeDeg});
        return shadowStateJson();
    }

    if (action == QStringLiteral("set_datetime")) {
        const double month = requireNumber(req, "month");
        const double day = requireNumber(req, "day");
        const double hourLocal = requireNumber(req, "hourLocal");
        kernel_.send(
            events::SetSunDateTimeRequested{static_cast<int>(month), static_cast<int>(day), hourLocal});
        return shadowStateJson();
    }

    if (action == QStringLiteral("set_light")) {
        const double light = requireNumber(req, "light");
        kernel_.send(events::SetShadowLightRequested{light});
        return shadowStateJson();
    }

    if (action == QStringLiteral("set_dark")) {
        const double dark = requireNumber(req, "dark");
        kernel_.send(events::SetShadowDarkRequested{dark});
        return shadowStateJson();
    }

    if (action == QStringLiteral("state")) {
        return shadowStateJson();
    }

    throw CommandError{QStringLiteral("unknown shadow action '%1'").arg(action)};
}

QJsonObject DebugBridge::cmdFog(const QJsonObject& req) {
    const QString action = requireString(req, "action");

    if (action == QStringLiteral("set_enabled")) {
        const bool value = requireBool(req, "enabled");
        QAction* action_ = window_->fogAction();
        if (!action_) {
            throw CommandError{QStringLiteral("no menu action for fog")};
        }
        if (action_->isChecked() != value) {
            action_->trigger();
            QCoreApplication::processEvents();
        }
        return fogStateJson();
    }

    if (action == QStringLiteral("set_range")) {
        // No UI control exists yet.
        const double startDistance = requireNumber(req, "startDistance");
        const double endDistance = requireNumber(req, "endDistance");
        kernel_.send(events::SetFogRangeRequested{startDistance, endDistance});
        return fogStateJson();
    }

    if (action == QStringLiteral("set_use_background_color")) {
        const bool value = requireBool(req, "useBackgroundColor");
        kernel_.send(events::SetFogUseBackgroundColorRequested{value});
        return fogStateJson();
    }

    if (action == QStringLiteral("state")) {
        return fogStateJson();
    }

    throw CommandError{QStringLiteral("unknown fog action '%1'").arg(action)};
}

namespace {

// Determinant of transform's 3x3 linear part -- the world-space volume
// scale factor (translation doesn't affect volume).
double transformDeterminant(const geo::Transform& t) {
    return geo::dot(t.col0, geo::cross(t.col1, t.col2));
}

}  // namespace

QJsonObject DebugBridge::cmdSolid(const QJsonObject& req) {
    // Defaults to "query" when omitted, so a bare `solid {instanceId}` keeps working.
    const QString action =
        req.contains(QStringLiteral("action")) ? requireString(req, "action") : QStringLiteral("query");

    if (action == QStringLiteral("apply")) {
        const QString opStr = requireString(req, "op");
        bool knownOp = false;
        const events::SolidOp op = solidOpFromString(opStr, knownOp);
        if (!knownOp) {
            throw CommandError{QStringLiteral("unknown solid op '%1'").arg(opStr)};
        }

        std::vector<geo::Id> ids;
        for (const QJsonValue& v : req.value(QStringLiteral("instanceIds")).toArray()) {
            ids.push_back(static_cast<geo::Id>(v.toDouble()));
        }

        auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);

        // Snapshot root Instance ids before the send -- a rejected op mutates
        // nothing, so before/after is the only success signal available here.
        std::vector<geo::Id> before;
        if (agent) {
            before.reserve(agent->scene().root().children.size());
            for (const geo::Instance& inst : agent->scene().root().children) before.push_back(inst.id);
        }

        kernel_.send(events::SolidOpRequested{op, ids});

        QJsonArray newIds;
        if (agent) {
            for (const geo::Instance& inst : agent->scene().root().children) {
                if (std::find(before.begin(), before.end(), inst.id) == before.end()) {
                    newIds.append(static_cast<qint64>(inst.id));
                }
            }
        }

        QJsonObject o;
        o["applied"] = !newIds.isEmpty();
        o["newInstanceIds"] = newIds;
        return o;
    }

    if (action != QStringLiteral("query")) {
        throw CommandError{QStringLiteral("unknown solid action '%1'").arg(action)};
    }

    const double idNum = requireNumber(req, "instanceId");

    QJsonObject o;
    auto agent = kernel_.agentAs<agent::GeometryApi>(agent::kGeometryApiName);
    if (!agent) {
        o["found"] = false;
        return o;
    }

    // A direct child of the root definition -- no nested-instance creation path yet.
    const geo::Scene& scene = agent->scene();
    const geo::Instance* inst = scene.findInstance(geo::kRootDefinitionId, static_cast<geo::Id>(idNum));
    if (inst == nullptr) {
        o["found"] = false;
        return o;
    }
    const geo::Definition* def = scene.definition(inst->definitionId);
    if (def == nullptr) {
        // Defensive only -- Scene guarantees a valid definitionId for any
        // instance findInstance can return; shouldn't happen in practice.
        o["found"] = false;
        return o;
    }

    o["found"] = true;
    // All classification math is geo::solid's -- this command only resolves
    // the instance and reshapes the result into JSON.
    const geo::SolidInfo info = geo::isSolid(def->model);
    o["solid"] = geo::isSolidDefinition(*def);
    o["wireEdges"] = info.wireEdges;
    o["nonManifoldVertex"] = info.nonManifoldVertex;
    o["nonPositiveVolume"] = info.nonPositiveVolume;
    const double modelVolume = geo::solidVolume(def->model);
    o["modelVolume"] = modelVolume;
    o["worldVolume"] = std::abs(transformDeterminant(inst->transform)) * modelVolume;

    std::size_t instanceCount = 0;
    for (const geo::Instance& child : scene.root().children) {
        if (child.definitionId == inst->definitionId) ++instanceCount;
    }
    o["instanceCount"] = static_cast<qint64>(instanceCount);

    return o;
}

}  // namespace plnr::devbridge
