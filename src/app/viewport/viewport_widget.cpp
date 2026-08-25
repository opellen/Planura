#include "viewport_widget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <QColor>
#include <QFontMetricsF>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QPolygonF>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector4D>
#include <QWheelEvent>

#include "geo_convert.h"
#include "render_log.h"

namespace plnr::viewport {

namespace {

struct LineVertex {
    float x, y, z;
    float r, g, b;
};

void addLine(std::vector<LineVertex>& out, const QVector3D& a, const QVector3D& b, const QVector3D& color) {
    out.push_back({a.x(), a.y(), a.z(), color.x(), color.y(), color.z()});
    out.push_back({b.x(), b.y(), b.z(), color.x(), color.y(), color.z()});
}

// Core profile has no line stipple, so a dashed axis half is faked as short
// on/off segments baked into the vertex buffer.
void addDashedLine(std::vector<LineVertex>& out, const QVector3D& from, const QVector3D& to, const QVector3D& color) {
    constexpr float kDashLen = 0.3f;
    constexpr float kGapLen = 0.3f;

    const QVector3D dir = to - from;
    const float totalLen = dir.length();
    if (totalLen < 1e-6f) return;
    const QVector3D unit = dir / totalLen;

    for (float t = 0.0f; t < totalLen; t += kDashLen + kGapLen) {
        const float segEnd = std::min(t + kDashLen, totalLen);
        addLine(out, from + unit * t, from + unit * segEnd, color);
    }
}

// Ground grid + axes triad in the given frame. Grid: 1-unit cells in the
// xDir/yDir plane, extent +-10. Axes: solid positive halves, dashed negative
// halves; xDir always draws red, yDir green, zDir blue, however they point.
void buildGridAndAxes(std::vector<LineVertex>& out, const QVector3D& origin, const QVector3D& xDir,
                       const QVector3D& yDir, const QVector3D& zDir) {
    const QVector3D gridColor(200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f);
    constexpr int kExtent = 10;

    for (int i = -kExtent; i <= kExtent; ++i) {
        if (i == 0) continue;  // skip the two lines coincident with the axes
        const QVector3D alongX = origin + xDir * static_cast<float>(i);
        addLine(out, alongX + yDir * static_cast<float>(-kExtent), alongX + yDir * static_cast<float>(kExtent), gridColor);
        const QVector3D alongY = origin + yDir * static_cast<float>(i);
        addLine(out, alongY + xDir * static_cast<float>(-kExtent), alongY + xDir * static_cast<float>(kExtent), gridColor);
    }

    constexpr float kAxisLen = 15.0f;

    const QVector3D red(1.0f, 0.0f, 0.0f);
    const QVector3D green(0.0f, 1.0f, 0.0f);
    const QVector3D blue(0.0f, 0.0f, 1.0f);
    const QVector3D redLight(1.0f, 0.65f, 0.65f);
    const QVector3D greenLight(0.65f, 1.0f, 0.65f);
    const QVector3D blueLight(0.65f, 0.65f, 1.0f);

    addLine(out, origin, origin + xDir * kAxisLen, red);
    addLine(out, origin, origin + yDir * kAxisLen, green);
    addLine(out, origin, origin + zDir * kAxisLen, blue);

    addDashedLine(out, origin, origin - xDir * kAxisLen, redLight);
    addDashedLine(out, origin, origin - yDir * kAxisLen, greenLight);
    addDashedLine(out, origin, origin - zDir * kAxisLen, blueLight);
}

const char* kVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMvp;
out vec3 vColor;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

const char* kFragmentShaderSrc = R"(#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

// Shared by model face fills and model edges. aPos is world-space, so
// vWorldPos feeds the clip test directly; aNormal (loc 2) is read only by face
// draws, and position-only consumers get the GL default (0,0,0).
const char* kFlatVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;
uniform mat4 uMvp;
out vec3 vWorldPos;
out vec3 vNormal;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vNormal = aNormal;
}
)";

// Shared uniform-color fragment shader for model faces/edges. The
// clip/shading/fog uniforms are ON only for model draws; overlays run with all
// three OFF. uLight 80 must reproduce the 0.45 diffuse constant EXACTLY.
const char* kFlatFragmentShaderSrc = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
uniform vec4 uColor;
uniform bool uClipEnable;
uniform vec4 uClipPlane;
uniform bool uShadingEnabled;
uniform vec3 uSunDir;
uniform float uLight;
uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uEyePos;
out vec4 fragColor;
void main() {
    if (uClipEnable && dot(vWorldPos, uClipPlane.xyz) > uClipPlane.w) discard;
    vec4 baseColor = uColor;
    if (uShadingEnabled) {
        vec3 n = gl_FrontFacing ? vNormal : -vNormal;
        float nLen = length(n);
        float ndotl = nLen > 1e-6 ? max(dot(n / nLen, uSunDir), 0.0) : 0.0;
        const float kShadingAmbient = 0.55;
        const float kShadingDiffuseAtDefaultLight = 0.45;
        const float kDefaultLight = 80.0;
        float diffuse = kShadingDiffuseAtDefaultLight * (uLight / kDefaultLight);
        baseColor.rgb *= (kShadingAmbient + diffuse * ndotl);
    }
    if (uFogEnabled) {
        float dist = length(vWorldPos - uEyePos);
        float fogFactor = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 1e-4), 0.0, 1.0);
        baseColor.rgb = mix(baseColor.rgb, uFogColor, fogFactor);
    }
    fragColor = baseColor;
}
)";

// kFlatFragmentShaderSrc VERBATIM plus exactly ONE addition -- a screen-space
// dot-grid discard at the top of main(). Keep the rest byte-identical.
const char* kStippleFragmentShaderSrc = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
uniform vec4 uColor;
uniform bool uClipEnable;
uniform vec4 uClipPlane;
uniform bool uShadingEnabled;
uniform vec3 uSunDir;
uniform float uLight;
uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uEyePos;
out vec4 fragColor;
void main() {
    const float kDotSpacingPx = 5.0;
    const float kDotSizePx = 2.0;
    vec2 dotCell = mod(gl_FragCoord.xy, kDotSpacingPx);
    if (dotCell.x >= kDotSizePx || dotCell.y >= kDotSizePx) discard;

    if (uClipEnable && dot(vWorldPos, uClipPlane.xyz) > uClipPlane.w) discard;
    vec4 baseColor = uColor;
    if (uShadingEnabled) {
        vec3 n = gl_FrontFacing ? vNormal : -vNormal;
        float nLen = length(n);
        float ndotl = nLen > 1e-6 ? max(dot(n / nLen, uSunDir), 0.0) : 0.0;
        const float kShadingAmbient = 0.55;
        const float kShadingDiffuseAtDefaultLight = 0.45;
        const float kDefaultLight = 80.0;
        float diffuse = kShadingDiffuseAtDefaultLight * (uLight / kDefaultLight);
        baseColor.rgb *= (kShadingAmbient + diffuse * ndotl);
    }
    if (uFogEnabled) {
        float dist = length(vWorldPos - uEyePos);
        float fogFactor = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 1e-4), 0.0, 1.0);
        baseColor.rgb = mix(baseColor.rgb, uFogColor, fogFactor);
    }
    fragColor = baseColor;
}
)";

// Minimal point shader for the preview snap marker: GL 3.3 core needs the
// vertex shader to write gl_PointSize (with GL_PROGRAM_POINT_SIZE enabled)
// for GL_POINTS to render at any size other than 1px.
const char* kPointVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
uniform float uPointSize;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
)";

const char* kPointFragmentShaderSrc = R"(#version 330 core
uniform vec3 uColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(uColor, 1.0);
}
)";

// Preview-batch line shader: reuses kFlatVertexShaderSrc's vertex stage, but
// uColor is vec4 (straight alpha).
const char* kPreviewFragmentShaderSrc = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

// Back-edges stipple, paired with kFlatVertexShaderSrc. Core profile has no
// line stipple, so the dashed look is a screen-space 4x4 checkerboard discard.
const char* kBackEdgeFragmentShaderSrc = R"(#version 330 core
in vec3 vWorldPos;
uniform vec4 uColor;
uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uEyePos;
out vec4 fragColor;
void main() {
    if (((int(gl_FragCoord.x) + int(gl_FragCoord.y)) / 4) % 2 == 0) discard;
    vec4 baseColor = uColor;
    if (uFogEnabled) {
        float dist = length(vWorldPos - uEyePos);
        float fogFactor = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 1e-4), 0.0, 1.0);
        baseColor.rgb = mix(baseColor.rgb, uFogColor, fogFactor);
    }
    fragColor = baseColor;
}
)";

// Thick-line quad expansion: each GL_LINES segment becomes 2 triangles, offset
// along the segment's screen-space perpendicular by aSide*uHalfWidthPx before
// the perspective divide, so z/w -- and depth -- stay the real endpoint's.
const char* kThickLineVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aThisEnd;
layout(location = 1) in vec3 aOtherEnd;
layout(location = 2) in float aSide;
uniform mat4 uMvp;
uniform vec2 uViewportSize;
uniform float uHalfWidthPx;
out vec3 vWorldPos;
void main() {
    vec4 clipThis = uMvp * vec4(aThisEnd, 1.0);
    vec4 clipOther = uMvp * vec4(aOtherEnd, 1.0);

    float wThis = abs(clipThis.w) > 1e-6 ? clipThis.w : 1e-6;
    float wOther = abs(clipOther.w) > 1e-6 ? clipOther.w : 1e-6;

    vec2 halfViewport = uViewportSize * 0.5;
    vec2 screenThis = (clipThis.xy / wThis) * halfViewport;
    vec2 screenOther = (clipOther.xy / wOther) * halfViewport;

    vec2 dir = screenOther - screenThis;
    float len = length(dir);
    vec2 perp = (len > 1e-6) ? normalize(vec2(-dir.y, dir.x)) : vec2(0.0, 1.0);

    vec2 offsetNdc = (perp * (aSide * uHalfWidthPx)) / halfViewport;
    gl_Position = vec4(clipThis.xy + offsetNdc * wThis, clipThis.z, clipThis.w);
    vWorldPos = aThisEnd;
}
)";

// A dedicated shader, NOT kFlatFragmentShaderSrc: that one declares `in vec3
// vNormal`, and a fragment INPUT with no matching vertex output is a link error.
const char* kThickLineFragmentShaderSrc = R"(#version 330 core
in vec3 vWorldPos;
uniform vec4 uColor;
uniform bool uClipEnable;
uniform vec4 uClipPlane;
uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uEyePos;
out vec4 fragColor;
void main() {
    if (uClipEnable && dot(vWorldPos, uClipPlane.xyz) > uClipPlane.w) discard;
    vec4 baseColor = uColor;
    if (uFogEnabled) {
        float dist = length(vWorldPos - uEyePos);
        float fogFactor = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 1e-4), 0.0, 1.0);
        baseColor.rgb = mix(baseColor.rgb, uFogColor, fogFactor);
    }
    fragColor = baseColor;
}
)";

// Textured face shader: pos loc 0 + uv loc 1, reusing the same vaoFaces_
// layout flatColorProgram_ uses (which simply never reads location 1).
const char* kTexturedVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec3 aNormal;
uniform mat4 uMvp;
out vec3 vWorldPos;
out vec2 vUv;
out vec3 vNormal;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vUv = aUv;
    vNormal = aNormal;
}
)";

// The sampled texture rgb REPLACES the material's flat rgb entirely; only
// alpha is modulated, by uOpacity (straight alpha, as in flatColorProgram_).
const char* kTexturedFragmentShaderSrc = R"(#version 330 core
in vec3 vWorldPos;
in vec2 vUv;
in vec3 vNormal;
uniform sampler2D uTexture;
uniform float uOpacity;
uniform bool uClipEnable;
uniform vec4 uClipPlane;
uniform bool uShadingEnabled;
uniform vec3 uSunDir;
uniform float uLight;
uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uEyePos;
out vec4 fragColor;
void main() {
    if (uClipEnable && dot(vWorldPos, uClipPlane.xyz) > uClipPlane.w) discard;
    vec4 texColor = texture(uTexture, vUv);
    vec3 rgb = texColor.rgb;
    if (uShadingEnabled) {
        vec3 n = gl_FrontFacing ? vNormal : -vNormal;
        float nLen = length(n);
        float ndotl = nLen > 1e-6 ? max(dot(n / nLen, uSunDir), 0.0) : 0.0;
        const float kShadingAmbient = 0.55;
        const float kShadingDiffuseAtDefaultLight = 0.45;
        const float kDefaultLight = 80.0;
        float diffuse = kShadingDiffuseAtDefaultLight * (uLight / kDefaultLight);
        rgb *= (kShadingAmbient + diffuse * ndotl);
    }
    if (uFogEnabled) {
        float dist = length(vWorldPos - uEyePos);
        float fogFactor = clamp((dist - uFogStart) / max(uFogEnd - uFogStart, 1e-4), 0.0, 1.0);
        rgb = mix(rgb, uFogColor, fogFactor);
    }
    fragColor = vec4(rgb, texColor.a * uOpacity);
}
)";

// Position-only: the flatten-onto-the-ground projection is NOT here -- paintGL
// composes buildShadowMatrix with the camera MVP and passes the result as uMvp.
const char* kShadowVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMvp;
void main() {
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

// Flat, uniformly colored. No stencil logic here: GL's stencil test,
// configured by paintGL, gates fragments before this shader runs.
const char* kShadowFragmentShaderSrc = R"(#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

// Shared fullscreen-triangle vertex shader for the AO screen-space passes and
// the sky pass: consumes vaoFullscreenTri_, deriving UV as aPos*0.5+0.5.
const char* kFullscreenVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUv = aPos * 0.5 + 0.5;
}
)";

// industry-standard default sky, Planura palette. kBackgroundColor is the single
// source of truth for glClearColor -- initializeGL's clear, paintGL's clear
// and setFog's fog-color mirror all read it, keep in sync.
const QVector3D kBackgroundColor(211.0f / 255.0f, 214.0f / 255.0f, 227.0f / 255.0f);
const QVector3D kSkyHorizonColor(229.0f / 255.0f, 233.0f / 255.0f, 252.0f / 255.0f);
const QVector3D kSkyZenithColor(158.0f / 255.0f, 175.0f / 255.0f, 233.0f / 255.0f);

// Fullscreen sky pass, the frame's first draw, depth test and writes off.
// Unprojects each fragment to a world ray: below the horizon = flat
// uGroundColor (== clear color), above = horizon->zenith mix over ~44 degrees.
const char* kSkyFragmentShaderSrc = R"(#version 330 core
uniform mat4 uInvVp;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform vec3 uGroundColor;
in vec2 vUv;
out vec4 fragColor;
void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 nearP = uInvVp * vec4(ndc, -1.0, 1.0);
    vec4 farP = uInvVp * vec4(ndc, 1.0, 1.0);
    vec3 dir = normalize(farP.xyz / farP.w - nearP.xyz / nearP.w);
    if (dir.z <= 0.0) {
        fragColor = vec4(uGroundColor, 1.0);
        return;
    }
    float t = clamp(dir.z / 0.7, 0.0, 1.0);
    fragColor = vec4(mix(uHorizonColor, uZenithColor, t), 1.0);
}
)";

// AO Pass A: renders vaoFaces_ into aoGeomFbo_'s depth + view-normal
// attachments. mat3(uView) rotates aNormal into view space with no
// inverse-transpose -- valid because this app's view matrix is orthonormal.
const char* kAoGeomVertexShaderSrc = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec3 aNormal;
uniform mat4 uView;
uniform mat4 uProjection;
out vec3 vWorldPos;
out vec3 vViewNormal;
void main() {
    vWorldPos = aPos;
    vViewNormal = mat3(uView) * aNormal;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
)";

// gl_FrontFacing flips the output normal since this pass draws with culling
// OFF -- a triangle facing away still needs its visible side recorded. A
// near-zero normal defensively faces the camera rather than producing NaN.
const char* kAoGeomFragmentShaderSrc = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vViewNormal;
uniform bool uClipEnable;
uniform vec4 uClipPlane;
out vec4 fragColor;
void main() {
    if (uClipEnable && dot(vWorldPos, uClipPlane.xyz) > uClipPlane.w) discard;
    vec3 n = gl_FrontFacing ? vViewNormal : -vViewNormal;
    float len = length(n);
    fragColor = vec4(len > 1e-6 ? n / len : vec3(0.0, 0.0, 1.0), 1.0);
}
)";

// AO Pass B: hemisphere-kernel occlusion estimate, half-res into aoRawTex_.
// kKernel and kRotation are FIXED, so output is deterministic; kRotation
// stands in for a random-rotation texture and Pass C's blur hides its repeat.
const char* kAoEstimateFragmentShaderSrc = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uDepthTex;
uniform sampler2D uNormalTex;
uniform mat4 uProjection;
uniform mat4 uInvProjection;
uniform float uRadius;
uniform float uBias;
out float fragColor;

const vec3 kKernel[16] = vec3[](
    vec3(0.017833, 0.000000, 0.099290),
    vec3(-0.024363, 0.022319, 0.102727),
    vec3(0.004215, -0.048029, 0.112039),
    vec3(0.040713, 0.053102, 0.126454),
    vec3(-0.089400, -0.015814, 0.145135),
    vec3(0.102079, -0.064934, 0.167161),
    vec3(-0.041124, 0.152980, 0.191509),
    vec3(-0.093960, -0.180914, 0.217024),
    vec3(0.242366, 0.088512, 0.242370),
    vec3(-0.297214, 0.122686, 0.265968),
    vec3(0.167419, -0.357764, 0.285879),
    vec3(0.143343, 0.456999, 0.299605),
    vec3(-0.496562, -0.287768, 0.303690),
    vec3(0.664527, -0.146094, 0.292796),
    vec3(-0.459446, 0.653515, 0.256939),
    vec3(-0.119483, -0.922043, 0.166988)
);

const vec2 kRotation[16] = vec2[](
    vec2(1.000000, 0.000000),
    vec2(0.923880, 0.382683),
    vec2(0.707107, 0.707107),
    vec2(0.382683, 0.923880),
    vec2(0.000000, 1.000000),
    vec2(-0.382683, 0.923880),
    vec2(-0.707107, 0.707107),
    vec2(-0.923880, 0.382683),
    vec2(-1.000000, 0.000000),
    vec2(-0.923880, -0.382683),
    vec2(-0.707107, -0.707107),
    vec2(-0.382683, -0.923880),
    vec2(-0.000000, -1.000000),
    vec2(0.382683, -0.923880),
    vec2(0.707107, -0.707107),
    vec2(0.923880, -0.382683)
);

vec3 aoViewPosFromDepth(vec2 uv, float depth01) {
    vec2 ndcXy = uv * 2.0 - 1.0;
    float ndcZ = depth01 * 2.0 - 1.0;
    vec4 clip = uInvProjection * vec4(ndcXy, ndcZ, 1.0);
    return clip.xyz / clip.w;
}

void main() {
    float depth01 = texture(uDepthTex, vUv).r;
    if (depth01 >= 0.999999) {
        fragColor = 1.0;  // far plane / no geometry here -- background stays untouched
        return;
    }
    vec3 normal = texture(uNormalTex, vUv).xyz;
    float normalLen = length(normal);
    if (normalLen < 1e-6) {
        fragColor = 1.0;
        return;
    }
    normal = normal / normalLen;

    vec3 fragPos = aoViewPosFromDepth(vUv, depth01);

    ivec2 px = ivec2(gl_FragCoord.xy);
    vec2 rotXy = kRotation[(px.x % 4) * 4 + (px.y % 4)];
    vec3 randomVec = vec3(rotXy, 0.0);
    vec3 tangent = randomVec - normal * dot(randomVec, normal);
    float tangentLen = length(tangent);
    tangent = tangentLen > 1e-4 ? tangent / tangentLen : normalize(cross(normal, vec3(0.0, 1.0, 0.0)));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec3 samplePos = fragPos + (tbn * kKernel[i]) * uRadius;
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        vec2 sampleUv = (offset.xy / offset.w) * 0.5 + 0.5;
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0) continue;

        float sampleDepth01 = texture(uDepthTex, sampleUv).r;
        vec3 sceneViewPos = aoViewPosFromDepth(sampleUv, sampleDepth01);

        float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(abs(fragPos.z - sceneViewPos.z), 1e-4));
        occlusion += (sceneViewPos.z >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }
    fragColor = 1.0 - (occlusion / 16.0);
}
)";

// AO Pass C: 4x4 box blur, half-res -- kills kRotation's repeating tile.
// texture() with CLAMP_TO_EDGE handles boundary taps without extra branching.
const char* kAoBlurFragmentShaderSrc = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uAoRawTex;
out float fragColor;
void main() {
    vec2 texel = 1.0 / vec2(textureSize(uAoRawTex, 0));
    float sum = 0.0;
    for (int x = -2; x <= 1; ++x) {
        for (int y = -2; y <= 1; ++y) {
            sum += texture(uAoRawTex, vUv + vec2(float(x), float(y)) * texel).r;
        }
    }
    fragColor = sum / 16.0;
}
)";

// AO Pass D: multiply-blend composite, full-res, onto the default framebuffer.
// uAoBlurTex is LINEAR-filtered, upsampling Pass C's half-res result. Under
// glBlendFunc(GL_ZERO,GL_SRC_COLOR) this output is the multiplier; 1.0 is a no-op.
const char* kAoCompositeFragmentShaderSrc = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uAoBlurTex;
uniform float uAoStrength;
out vec4 fragColor;
void main() {
    float ao = texture(uAoBlurTex, vUv).r;
    float factor = mix(1.0, ao, uAoStrength);
    fragColor = vec4(factor, factor, factor, 1.0);
}
)";

const QVector3D kGridBoundsMin(-10.0f, -10.0f, 0.0f);
const QVector3D kGridBoundsMax(10.0f, 10.0f, 2.0f);

// Ground-shadow gate: sunDirection_.z() (= sin(altitude)) below this skips the
// pass -- buildShadowMatrix's 1/sunDir.z() term blows up near the horizon.
// Simplification: the reference modeler still casts very long shadows there.
constexpr float kMinSunElevationForShadows = 0.05f;

// Flattens a world point onto the z=0 ground plane along the sun direction.
// sunDirWorld points FROM a surface TOWARD the sun, so P'.xy = P.xy -
// (sunDir.xy/sunDir.z)*P.z -- linear, composing with the camera MVP by multiply.
QMatrix4x4 buildShadowMatrix(const QVector3D& sunDirWorld) {
    QMatrix4x4 m;  // identity
    const float sz = sunDirWorld.z();
    // Guard: the caller's elevation gate should keep sz well away from zero.
    const float safeSz = std::abs(sz) > 1e-4f ? sz : 1e-4f;
    const float kx = sunDirWorld.x() / safeSz;
    const float ky = sunDirWorld.y() / safeSz;
    m(0, 2) = -kx;
    m(1, 2) = -ky;
    m(2, 2) = 0.0f;  // zeroes P.z's own contribution to the result -- P'.z is always exactly 0
    return m;
}

// Model edges: pure black.
const QVector3D kModelEdgeColor(0.0f, 0.0f, 0.0f);

// Back-edges pass's subdued mid-gray -- distinct from the black model edges
// and the lighter kDimmedEdgeColor. UNVERIFIED against a the reference modeler screenshot.
const QVector3D kBackEdgeColor(0.55f, 0.55f, 0.55f);

// Profile-weight edges' TOTAL on-screen width in pixels. Realized by
// thickLineProgram_'s quad expansion (paintGL passes half of it), not glLineWidth.
constexpr float kProfileEdgeWidth = 2.0f;

// Index 0 = nearest band (thickest) .. 2 = farthest, matching
// classifyAndBucketEdges' band convention. Total pixel widths, as for
// kProfileEdgeWidth; band 2's 1.0 is just the plain GL_LINES default.
constexpr float kEdgeDepthBandWidths[3] = {3.0f, 2.0f, 1.0f};

// HiddenLine fills every face in this flat near-white regardless of material,
// still depth-writing so nearer faces occlude. UNVERIFIED against the reference modeler.
const QVector3D kHiddenLineFillColor(1.0f, 1.0f, 1.0f);

// X-Ray's global fill alpha, applied to EVERY range regardless of material.
// KNOWN CAVEAT: the fill renders flat OPAQUE white despite verified-correct
// GL state -- see temp/code/comment-sweep/flagged-traps-viewport.md.
constexpr float kXRayAlpha = 0.5f;

// AO tuning: kAoSampleRadius/kAoBias are view-space world units (no model
// matrix, so view scale == world scale) -- the kernel's reach and the
// self-occlusion-acne guard. Passes B/C run half-res, Pass A stays full-res.
constexpr float kAoSampleRadius = 0.5f;
constexpr float kAoBias = 0.01f;
constexpr int kAoResolutionDivisor = 2;

// Preview snap marker. Preview LINE color is not fixed -- each PreviewBatch
// carries its own RGBA.
const QVector3D kPreviewMarkerColor(0.2f, 0.75f, 0.2f);
constexpr float kPreviewMarkerPointSize = 8.0f;

// Editing-context dimmed overlay: light gray for geometry outside the
// current editing context.
const QVector3D kDimmedFaceColor(0.85f, 0.85f, 0.85f);
const QVector3D kDimmedEdgeColor(0.65f, 0.65f, 0.65f);

// Muted mid-gray for guides. UNVERIFIED against a the reference modeler screenshot.
const QVector3D kGuideColor(0.35f, 0.35f, 0.35f);

// Associated dimensions: dark near-black gray. Non-associated ones render in
// the app's axis/warning red (tool.h's kAxisRedColor).
const QVector3D kAnnotationColor(0.15f, 0.15f, 0.15f);
const QVector3D kAnnotationInvalidColor(0.86f, 0.20f, 0.18f);

// Leader-text label background: light box, distinct from the inference
// ScreenTip's light-yellow one. UNVERIFIED against a the reference modeler screenshot.
const QColor kAnnotationLabelBgColor(255, 255, 255, 235);
const QColor kAnnotationLabelBorderColor(140, 140, 140);
const QColor kAnnotationLabelTextColor(Qt::black);
constexpr qreal kAnnotationLeaderOffsetXPx = 16.0;
constexpr qreal kAnnotationLeaderOffsetYPx = -18.0;
constexpr qreal kAnnotationAnchorDotRadiusPx = 2.5;

// Section-plane widget: light semi-transparent green, UNVERIFIED exact RGBA.
// Active vs. inactive differ in alpha only.
const QVector3D kSectionColor(0.35f, 0.85f, 0.45f);
constexpr float kSectionActiveAlpha = 0.75f;
constexpr float kSectionInactiveAlpha = 0.35f;

// industry-standard blue for selected edges/vertices, the face-fill dot stipple,
// and the Push/Pull hover face -- all the same blue.
const QVector3D kSelectionColor(0.13f, 0.45f, 0.90f);
constexpr float kSelectionPointSize = 8.0f;

// Drag-selection rubber band: dark gray, solid. the reference modeler dashes the crossing case.
const QVector3D kScreenRectColor(0.2f, 0.2f, 0.2f);

// Inference-cue ScreenTip: the reference modeler's light-yellow tooltip box, dark-yellow
// border, black text -- switches to plain red text (no box) for warning.
const QColor kScreenTipBgColor(255, 255, 225);
const QColor kScreenTipBorderColor(120, 110, 60);
const QColor kScreenTipTextColor(Qt::black);
const QColor kScreenTipWarningColor(200, 30, 30);
constexpr qreal kMarkerGlyphHalfExtentPx = 4.0;
constexpr qreal kScreenTipOffsetXPx = 10.0;
constexpr qreal kScreenTipOffsetYPx = 14.0;

// Trace-line dash length in screen pixels -- converted to world units each
// frame via Camera::worldPerPixel so it stays a constant size on screen.
constexpr float kCueTraceDashLenPx = 8.0f;

// World-space dash segments from `from` to `to` (xyz per vertex, GL_LINES).
// Dash length derives from worldPerPixel for a constant ~8px on screen.
void buildDashedTrace(std::vector<float>& out, const QVector3D& from, const QVector3D& to, double worldPerPixel) {
    const float dashLen = static_cast<float>(worldPerPixel * kCueTraceDashLenPx);
    const QVector3D dir = to - from;
    const float totalLen = dir.length();
    if (totalLen < 1e-6f || dashLen < 1e-6f) return;
    const QVector3D unit = dir / totalLen;

    for (float t = 0.0f; t < totalLen; t += dashLen + dashLen) {
        const float segEnd = std::min(t + dashLen, totalLen);
        out.push_back(from.x() + unit.x() * t);
        out.push_back(from.y() + unit.y() * t);
        out.push_back(from.z() + unit.z() * t);
        out.push_back(from.x() + unit.x() * segEnd);
        out.push_back(from.y() + unit.y() * segEnd);
        out.push_back(from.z() + unit.z() * segEnd);
    }
}

// Expands a GL_LINES buffer into kThickLineVertexShaderSrc's format: 6
// vertices per input segment, each {thisEnd(3f), otherEnd(3f), side(1f)}.
void appendThickLineSegments(std::vector<float>& out, const std::vector<float>& lineVerts) {
    const std::size_t segmentCount = lineVerts.size() / 6;
    out.reserve(out.size() + segmentCount * 6 * 7);
    for (std::size_t s = 0; s < segmentCount; ++s) {
        const float* p0 = &lineVerts[s * 6];
        const float* p1 = &lineVerts[s * 6 + 3];
        const auto appendVertex = [&out](const float* self, const float* other, float side) {
            out.insert(out.end(), self, self + 3);
            out.insert(out.end(), other, other + 3);
            out.push_back(side);
        };
        // Quad corners A+/A-/B+/B- (side +1/-1 at each of the segment's two
        // endpoints), triangulated unindexed as (A+,A-,B+) then (A-,B+,B-).
        appendVertex(p0, p1, 1.0f);
        appendVertex(p0, p1, -1.0f);
        appendVertex(p1, p0, 1.0f);
        appendVertex(p0, p1, -1.0f);
        appendVertex(p1, p0, 1.0f);
        appendVertex(p1, p0, -1.0f);
    }
}

}  // namespace

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    format.setDepthBufferSize(24);
    // The ground-shadow pass's "darken each pixel once" technique needs a
    // stencil buffer; 8 bits is more than the 1 needed, but is the smallest
    // size documented as broadly supported alongside 24-bit depth.
    format.setStencilBufferSize(8);
    // No per-pixel alpha channel, so this framebuffer can never be misread as
    // translucent against the OS window behind it.
    format.setAlphaBufferSize(0);
    setFormat(format);

    setFocusPolicy(Qt::StrongFocus);
    // Needed so mouseMoveEvent fires on hover, not just during drags -- tools
    // need hover feedback before the user commits to a click.
    setMouseTracking(true);
}

ViewportWidget::~ViewportWidget() {
    makeCurrent();
    glDeleteVertexArrays(1, &vao_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vaoModelEdges_);
    glDeleteBuffers(1, &vboModelEdges_);
    glDeleteVertexArrays(1, &vaoProfileEdges_);
    glDeleteBuffers(1, &vboProfileEdges_);
    for (int i = 0; i < 3; ++i) {
        glDeleteVertexArrays(1, &vaoBandEdges_[static_cast<std::size_t>(i)]);
        glDeleteBuffers(1, &vboBandEdges_[static_cast<std::size_t>(i)]);
    }
    glDeleteVertexArrays(1, &vaoProfileEdgesThick_);
    glDeleteBuffers(1, &vboProfileEdgesThick_);
    for (int i = 0; i < 2; ++i) {
        glDeleteVertexArrays(1, &vaoBandEdgesThick_[static_cast<std::size_t>(i)]);
        glDeleteBuffers(1, &vboBandEdgesThick_[static_cast<std::size_t>(i)]);
    }
    glDeleteVertexArrays(1, &vaoFaces_);
    glDeleteBuffers(1, &vboFaces_);
    glDeleteVertexArrays(1, &vaoDimmedEdges_);
    glDeleteBuffers(1, &vboDimmedEdges_);
    glDeleteVertexArrays(1, &vaoDimmedFaces_);
    glDeleteBuffers(1, &vboDimmedFaces_);
    glDeleteVertexArrays(1, &vaoGuides_);
    glDeleteBuffers(1, &vboGuides_);
    glDeleteVertexArrays(1, &vaoAnnotationLines_);
    glDeleteBuffers(1, &vboAnnotationLines_);
    glDeleteVertexArrays(1, &vaoAnnotationLinesInvalid_);
    glDeleteBuffers(1, &vboAnnotationLinesInvalid_);
    glDeleteVertexArrays(1, &vaoSectionActive_);
    glDeleteBuffers(1, &vboSectionActive_);
    glDeleteVertexArrays(1, &vaoSectionInactive_);
    glDeleteBuffers(1, &vboSectionInactive_);
    for (PreviewBatchGL& batch : previewBatches_) {
        glDeleteVertexArrays(1, &batch.vao);
        glDeleteBuffers(1, &batch.vbo);
    }
    glDeleteVertexArrays(1, &vaoPreviewMarker_);
    glDeleteBuffers(1, &vboPreviewMarker_);
    glDeleteVertexArrays(1, &vaoCueTrace_);
    glDeleteBuffers(1, &vboCueTrace_);
    glDeleteVertexArrays(1, &vaoSelectionFaces_);
    glDeleteBuffers(1, &vboSelectionFaces_);
    glDeleteVertexArrays(1, &vaoSelectionEdges_);
    glDeleteBuffers(1, &vboSelectionEdges_);
    glDeleteVertexArrays(1, &vaoSelectionPoints_);
    glDeleteBuffers(1, &vboSelectionPoints_);
    // Push/Pull hover-face overlay.
    glDeleteVertexArrays(1, &vaoHoverFaces_);
    glDeleteBuffers(1, &vboHoverFaces_);
    glDeleteVertexArrays(1, &vaoScreenRect_);
    glDeleteBuffers(1, &vboScreenRect_);
    // AO pipeline GL objects -- destroyed under the same makeCurrent bracket
    // as the texture cache below.
    glDeleteFramebuffers(1, &aoGeomFbo_);
    glDeleteTextures(1, &aoDepthTex_);
    glDeleteTextures(1, &aoNormalTex_);
    glDeleteFramebuffers(1, &aoEstimateFbo_);
    glDeleteTextures(1, &aoRawTex_);
    glDeleteFramebuffers(1, &aoBlurFbo_);
    glDeleteTextures(1, &aoBlurTex_);
    glDeleteVertexArrays(1, &vaoFullscreenTri_);
    glDeleteBuffers(1, &vboFullscreenTri_);
    delete program_;
    delete flatColorProgram_;
    delete texturedProgram_;
    delete backEdgeProgram_;
    delete stippleProgram_;
    delete skyProgram_;
    delete thickLineProgram_;
    delete shadowProgram_;
    delete aoGeomProgram_;
    delete aoEstimateProgram_;
    delete aoBlurProgram_;
    delete aoCompositeProgram_;
    delete pointProgram_;
    delete previewLineProgram_;
    // QOpenGLTexture objects must be destroyed while this widget's context is
    // still current, hence the explicit clear() before doneCurrent().
    textureCache_.clear();
    doneCurrent();
}

QSize ViewportWidget::minimumSizeHint() const {
    return QSize(400, 300);
}

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();

    // The below-horizon background. Only ever visible if skyProgram_ failed to
    // link -- the sky pass otherwise covers every pixel.
    glClearColor(kBackgroundColor.x(), kBackgroundColor.y(), kBackgroundColor.z(), 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);  // lets kPointVertexShaderSrc set gl_PointSize

    glFrontFace(GL_CCW);  // matches geo::triangulate's winding convention

    program_ = new QOpenGLShaderProgram();
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShaderSrc);
    program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderSrc);
    program_->link();

    flatColorProgram_ = new QOpenGLShaderProgram();
    flatColorProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFlatVertexShaderSrc);
    flatColorProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFragmentShaderSrc);
    flatColorProgram_->link();

    pointProgram_ = new QOpenGLShaderProgram();
    pointProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kPointVertexShaderSrc);
    pointProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kPointFragmentShaderSrc);
    pointProgram_->link();

    previewLineProgram_ = new QOpenGLShaderProgram();
    previewLineProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFlatVertexShaderSrc);
    previewLineProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kPreviewFragmentShaderSrc);
    previewLineProgram_->link();

    // uTexture set ONCE here (unit 0, QOpenGLTexture::bind()'s default): a
    // sampler uniform persists across bind/release, so paintGL never re-sets it.
    texturedProgram_ = new QOpenGLShaderProgram();
    texturedProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kTexturedVertexShaderSrc);
    texturedProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kTexturedFragmentShaderSrc);
    texturedProgram_->link();
    texturedProgram_->bind();
    texturedProgram_->setUniformValue("uTexture", 0);
    texturedProgram_->release();

    // Back-edges stipple program -- see backEdgeProgram_'s header comment.
    backEdgeProgram_ = new QOpenGLShaderProgram();
    backEdgeProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFlatVertexShaderSrc);
    backEdgeProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kBackEdgeFragmentShaderSrc);
    backEdgeProgram_->link();

    // Selection/hover-face stipple program -- see stippleProgram_ in the header.
    stippleProgram_ = new QOpenGLShaderProgram();
    stippleProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFlatVertexShaderSrc);
    stippleProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kStippleFragmentShaderSrc);
    stippleProgram_->link();

    // Sky/background gradient program; its vertex stage reuses the AO
    // pipeline's shared fullscreen triangle, uploaded unconditionally below.
    skyProgram_ = new QOpenGLShaderProgram();
    skyProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFullscreenVertexShaderSrc);
    skyProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kSkyFragmentShaderSrc);
    skyProgram_->link();

    // Thick-line quad-expansion program -- kThickLineFragmentShaderSrc rather
    // than the flat one (see that shader's own comment for why).
    thickLineProgram_ = new QOpenGLShaderProgram();
    thickLineProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kThickLineVertexShaderSrc);
    thickLineProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kThickLineFragmentShaderSrc);
    thickLineProgram_->link();

    // Ground-shadow planar-projection program.
    shadowProgram_ = new QOpenGLShaderProgram();
    shadowProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kShadowVertexShaderSrc);
    shadowProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kShadowFragmentShaderSrc);
    shadowProgram_->link();

    // SSAO pipeline programs -- see renderAmbientOcclusion for the architecture.
    aoGeomProgram_ = new QOpenGLShaderProgram();
    aoGeomProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kAoGeomVertexShaderSrc);
    aoGeomProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kAoGeomFragmentShaderSrc);
    aoGeomProgram_->link();

    aoEstimateProgram_ = new QOpenGLShaderProgram();
    aoEstimateProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFullscreenVertexShaderSrc);
    aoEstimateProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kAoEstimateFragmentShaderSrc);
    aoEstimateProgram_->link();
    // Sampler uniforms set ONCE here, on units 0/1 -- the bind units
    // renderAmbientOcclusion uses.
    aoEstimateProgram_->bind();
    aoEstimateProgram_->setUniformValue("uDepthTex", 0);
    aoEstimateProgram_->setUniformValue("uNormalTex", 1);
    aoEstimateProgram_->release();

    aoBlurProgram_ = new QOpenGLShaderProgram();
    aoBlurProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFullscreenVertexShaderSrc);
    aoBlurProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kAoBlurFragmentShaderSrc);
    aoBlurProgram_->link();
    aoBlurProgram_->bind();
    aoBlurProgram_->setUniformValue("uAoRawTex", 0);
    aoBlurProgram_->release();

    aoCompositeProgram_ = new QOpenGLShaderProgram();
    aoCompositeProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, kFullscreenVertexShaderSrc);
    aoCompositeProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, kAoCompositeFragmentShaderSrc);
    aoCompositeProgram_->link();
    aoCompositeProgram_->bind();
    aoCompositeProgram_->setUniformValue("uAoBlurTex", 0);
    aoCompositeProgram_->release();

    // Shared fullscreen-triangle geometry, position-only, 2 floats/vertex.
    // Uploaded ONCE (GL_STATIC_DRAW); no dirty flag.
    {
        constexpr float kFullscreenTriVerts[6] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        glGenVertexArrays(1, &vaoFullscreenTri_);
        glGenBuffers(1, &vboFullscreenTri_);
        glBindVertexArray(vaoFullscreenTri_);
        glBindBuffer(GL_ARRAY_BUFFER, vboFullscreenTri_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kFullscreenTriVerts), kFullscreenTriVerts, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    buildSceneGeometry();

    // Model-geometry VAOs/VBOs: empty for now, position-only, GL_DYNAMIC_DRAW;
    // filled lazily by uploadModelGeometryIfDirty().
    glGenVertexArrays(1, &vaoModelEdges_);
    glGenBuffers(1, &vboModelEdges_);
    glBindVertexArray(vaoModelEdges_);
    glBindBuffer(GL_ARRAY_BUFFER, vboModelEdges_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Edge-style classification VAOs/VBOs: same empty position-only pattern.
    glGenVertexArrays(1, &vaoProfileEdges_);
    glGenBuffers(1, &vboProfileEdges_);
    glBindVertexArray(vaoProfileEdges_);
    glBindBuffer(GL_ARRAY_BUFFER, vboProfileEdges_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    for (int i = 0; i < 3; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        glGenVertexArrays(1, &vaoBandEdges_[idx]);
        glGenBuffers(1, &vboBandEdges_[idx]);
        glBindVertexArray(vaoBandEdges_[idx]);
        glBindBuffer(GL_ARRAY_BUFFER, vboBandEdges_[idx]);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    }

    // Thick-line quad VAOs/VBOs: {thisEnd(3f), otherEnd(3f), side(1f)} per
    // vertex, stride 7. Only the profile bucket and the nearest two bands
    // get one.
    glGenVertexArrays(1, &vaoProfileEdgesThick_);
    glGenBuffers(1, &vboProfileEdgesThick_);
    glBindVertexArray(vaoProfileEdgesThick_);
    glBindBuffer(GL_ARRAY_BUFFER, vboProfileEdgesThick_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    for (int i = 0; i < 2; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        glGenVertexArrays(1, &vaoBandEdgesThick_[idx]);
        glGenBuffers(1, &vboBandEdgesThick_[idx]);
        glBindVertexArray(vaoBandEdgesThick_[idx]);
        glBindBuffer(GL_ARRAY_BUFFER, vboBandEdgesThick_[idx]);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    // Stride 8 floats/vertex (position + uv + world normal) for EVERY triangle,
    // textured or not. Attribute 1 (uv) is read by texturedProgram_ only,
    // attribute 2 (normal) by both; unread attributes still link and run.
    glGenVertexArrays(1, &vaoFaces_);
    glGenBuffers(1, &vboFaces_);
    glBindVertexArray(vaoFaces_);
    glBindBuffer(GL_ARRAY_BUFFER, vboFaces_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Dimmed-geometry overlay VAOs/VBOs: same position-only dynamic pattern.
    glGenVertexArrays(1, &vaoDimmedEdges_);
    glGenBuffers(1, &vboDimmedEdges_);
    glBindVertexArray(vaoDimmedEdges_);
    glBindBuffer(GL_ARRAY_BUFFER, vboDimmedEdges_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &vaoDimmedFaces_);
    glGenBuffers(1, &vboDimmedFaces_);
    glBindVertexArray(vaoDimmedFaces_);
    glBindBuffer(GL_ARRAY_BUFFER, vboDimmedFaces_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Guide overlay VAO/VBO: same position-only dynamic pattern.
    glGenVertexArrays(1, &vaoGuides_);
    glGenBuffers(1, &vboGuides_);
    glBindVertexArray(vaoGuides_);
    glBindBuffer(GL_ARRAY_BUFFER, vboGuides_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Annotation overlay VAOs/VBOs: same position-only dynamic pattern.
    glGenVertexArrays(1, &vaoAnnotationLines_);
    glGenBuffers(1, &vboAnnotationLines_);
    glBindVertexArray(vaoAnnotationLines_);
    glBindBuffer(GL_ARRAY_BUFFER, vboAnnotationLines_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &vaoAnnotationLinesInvalid_);
    glGenBuffers(1, &vboAnnotationLinesInvalid_);
    glBindVertexArray(vaoAnnotationLinesInvalid_);
    glBindBuffer(GL_ARRAY_BUFFER, vboAnnotationLinesInvalid_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Section-plane overlay VAOs/VBOs: same position-only dynamic pattern.
    glGenVertexArrays(1, &vaoSectionActive_);
    glGenBuffers(1, &vboSectionActive_);
    glBindVertexArray(vaoSectionActive_);
    glBindBuffer(GL_ARRAY_BUFFER, vboSectionActive_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &vaoSectionInactive_);
    glGenBuffers(1, &vboSectionInactive_);
    glBindVertexArray(vaoSectionInactive_);
    glBindBuffer(GL_ARRAY_BUFFER, vboSectionInactive_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Preview line-batch VAOs/VBOs are NOT pre-created: the batch count varies
    // per setPreviewBatches call, so uploadPreviewIfDirty() sizes the pool.
    glGenVertexArrays(1, &vaoPreviewMarker_);
    glGenBuffers(1, &vboPreviewMarker_);
    glBindVertexArray(vaoPreviewMarker_);
    glBindBuffer(GL_ARRAY_BUFFER, vboPreviewMarker_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Inference-cue trace VAO/VBO: position-only, rebuilt every active frame
    // by uploadCueTraceIfActive() instead of using a dirty flag.
    glGenVertexArrays(1, &vaoCueTrace_);
    glGenBuffers(1, &vboCueTrace_);
    glBindVertexArray(vaoCueTrace_);
    glBindBuffer(GL_ARRAY_BUFFER, vboCueTrace_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Selection-highlight overlay VAOs/VBOs: same position-only dynamic pattern.
    glGenVertexArrays(1, &vaoSelectionFaces_);
    glGenBuffers(1, &vboSelectionFaces_);
    glBindVertexArray(vaoSelectionFaces_);
    glBindBuffer(GL_ARRAY_BUFFER, vboSelectionFaces_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &vaoSelectionEdges_);
    glGenBuffers(1, &vboSelectionEdges_);
    glBindVertexArray(vaoSelectionEdges_);
    glBindBuffer(GL_ARRAY_BUFFER, vboSelectionEdges_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    glGenVertexArrays(1, &vaoSelectionPoints_);
    glGenBuffers(1, &vboSelectionPoints_);
    glBindVertexArray(vaoSelectionPoints_);
    glBindBuffer(GL_ARRAY_BUFFER, vboSelectionPoints_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Push/Pull hover-face overlay VAO/VBO: same pattern as vaoSelectionFaces_.
    glGenVertexArrays(1, &vaoHoverFaces_);
    glGenBuffers(1, &vboHoverFaces_);
    glBindVertexArray(vaoHoverFaces_);
    glBindBuffer(GL_ARRAY_BUFFER, vboHoverFaces_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    // Screen-rect overlay VAO/VBO: 4 verts (GL_LINE_LOOP), position-only,
    // rebuilt by uploadScreenRectIfDirty() when the corners change.
    glGenVertexArrays(1, &vaoScreenRect_);
    glGenBuffers(1, &vboScreenRect_);
    glBindVertexArray(vaoScreenRect_);
    glBindBuffer(GL_ARRAY_BUFFER, vboScreenRect_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    axesFrameDirty_ = true;      // covers the default (or already-set) axes frame
    modelGeometryDirty_ = true;  // covers geometry cached before initializeGL ran
    dimmedGeometryDirty_ = true; // covers dimmed geometry cached before initializeGL ran
    guideGeometryDirty_ = true;  // covers guide geometry cached before initializeGL ran
    annotationGeometryDirty_ = true;  // covers annotation geometry cached before initializeGL ran
    sectionGeometryDirty_ = true;  // covers section-plane geometry cached before initializeGL ran
    previewDirty_ = true;        // covers preview data cached before initializeGL ran
    selectionDirty_ = true;      // covers selection data cached before initializeGL ran
    hoverFaceDirty_ = true;      // covers hover-face data cached before initializeGL ran
    screenRectDirty_ = true;     // covers a screen rect set before initializeGL ran
}

void ViewportWidget::buildSceneGeometry() {
    // Just allocates the VAO/VBO (position+color layout) -- the vertex data
    // is generated later by uploadAxesGeometryIfDirty().
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void*>(offsetof(LineVertex, r)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void ViewportWidget::setAxesFrame(const QVector3D& origin, const QVector3D& xDir, const QVector3D& yDir,
                                   const QVector3D& zDir) {
    pendingAxesOrigin_ = origin;
    pendingAxesXDir_ = xDir;
    pendingAxesYDir_ = yDir;
    pendingAxesZDir_ = zDir;
    axesFrameDirty_ = true;
    update();
}

void ViewportWidget::uploadAxesGeometryIfDirty() {
    if (!axesFrameDirty_) return;

    std::vector<LineVertex> vertices;
    buildGridAndAxes(vertices, pendingAxesOrigin_, pendingAxesXDir_, pendingAxesYDir_, pendingAxesZDir_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(vertices.size() * sizeof(LineVertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    vertexCount_ = static_cast<int>(vertices.size());
    axesFrameDirty_ = false;
}

void ViewportWidget::setModelGeometry(std::vector<float> edgeVerts, std::vector<float> faceTris,
                                       std::vector<MaterialRange> opaqueRanges,
                                       std::vector<MaterialRange> transparentRanges,
                                       std::unordered_map<geo::Id, MaterialSwatch> materialSwatches) {
    pendingEdgeVerts_ = std::move(edgeVerts);
    pendingFaceTris_ = std::move(faceTris);
    pendingOpaqueRanges_ = std::move(opaqueRanges);
    pendingTransparentRanges_ = std::move(transparentRanges);
    pendingMaterialSwatches_ = std::move(materialSwatches);
    modelGeometryDirty_ = true;
    update();
}

void ViewportWidget::setEdgeStyleGeometry(std::vector<float> profileVerts, std::array<std::vector<float>, 3> bandVerts) {
    pendingProfileEdgeVerts_ = std::move(profileVerts);
    pendingBandEdgeVerts_ = std::move(bandVerts);
    edgeStyleGeometryDirty_ = true;
    update();
}

void ViewportWidget::setDimmedGeometry(std::vector<float> edgeVerts, std::vector<float> faceTris) {
    pendingDimmedEdgeVerts_ = std::move(edgeVerts);
    pendingDimmedFaceTris_ = std::move(faceTris);
    dimmedGeometryDirty_ = true;
    update();
}

void ViewportWidget::setGuideGeometry(std::vector<float> lineVerts) {
    pendingGuideVerts_ = std::move(lineVerts);
    guideGeometryDirty_ = true;
    update();
}

void ViewportWidget::setAnnotationGeometry(std::vector<float> normalLineVerts, std::vector<float> invalidLineVerts,
                                            std::vector<AnnotationLabel> labels) {
    pendingAnnotationLineVerts_ = std::move(normalLineVerts);
    pendingAnnotationLineInvalidVerts_ = std::move(invalidLineVerts);
    pendingAnnotationLabels_ = std::move(labels);
    annotationGeometryDirty_ = true;
    update();
}

void ViewportWidget::setSectionGeometry(std::vector<float> activeLineVerts, std::vector<float> inactiveLineVerts) {
    pendingSectionActiveVerts_ = std::move(activeLineVerts);
    pendingSectionInactiveVerts_ = std::move(inactiveLineVerts);
    sectionGeometryDirty_ = true;
    update();
}

void ViewportWidget::setClipPlane(std::optional<ClipPlane> plane) {
    clipPlane_ = plane;
    update();
}

void ViewportWidget::setStyle(FaceStyle style, bool profiles, bool depthCue, bool backEdges, bool ambientOcclusion,
                               float aoStrength, const QVector3D& defaultFrontColor,
                               const QVector3D& defaultBackColor) {
    style_ = style;
    profiles_ = profiles;
    depthCue_ = depthCue;
    backEdges_ = backEdges;
    ambientOcclusion_ = ambientOcclusion;
    aoStrength_ = aoStrength;
    styleFrontColor_ = defaultFrontColor;
    styleBackColor_ = defaultBackColor;
    update();
}

void ViewportWidget::setShadowState(bool showShadows, bool useSunForShading, const QVector3D& sunDirWorld,
                                     float light, float dark) {
    showShadows_ = showShadows;
    useSunForShading_ = useSunForShading;
    sunDirection_ = sunDirWorld;
    shadowLight_ = light;
    shadowDark_ = dark;
    update();
}

void ViewportWidget::setFog(bool enabled, float startDistance, float endDistance, bool useBackgroundColor,
                             const QVector3D& customColor) {
    fogEnabled_ = enabled;
    fogStartDistance_ = startDistance;
    fogEndDistance_ = endDistance;
    // Resolved to kBackgroundColor here, so uFogColor is always a plain RGB.
    // Distant geometry fades toward the GROUND color, not the sky gradient.
    fogColor_ = useBackgroundColor ? kBackgroundColor : customColor;
    update();
}

void ViewportWidget::setPreviewBatches(std::vector<PreviewBatch> batches, std::optional<QVector3D> marker) {
    pendingPreviewBatches_ = std::move(batches);
    pendingPreviewMarker_ = marker;
    previewDirty_ = true;
    update();
}

void ViewportWidget::setInferenceCue(std::optional<InferenceCue> cue) {
    // No dirty flag: the marker/tip are QPainter-drawn per frame, and the
    // trace line's tiny buffer is rebuilt each active frame to track live zoom.
    inferenceCue_ = std::move(cue);
    update();
}

void ViewportWidget::setSelectionGeometry(std::vector<float> edgeVerts, std::vector<float> faceTris, std::vector<float> pointVerts) {
    pendingSelectionEdgeVerts_ = std::move(edgeVerts);
    pendingSelectionFaceTris_ = std::move(faceTris);
    pendingSelectionPointVerts_ = std::move(pointVerts);
    selectionDirty_ = true;
    update();
}

void ViewportWidget::setHoverFaceTris(std::vector<float> faceTris) {
    pendingHoverFaceTris_ = std::move(faceTris);
    hoverFaceDirty_ = true;
    update();
}

void ViewportWidget::setScreenRect(std::optional<QPointF> a, std::optional<QPointF> b) {
    pendingScreenRectA_ = a;
    pendingScreenRectB_ = b;
    screenRectDirty_ = true;
    update();
}

void ViewportWidget::setCameraState(const QVector3D& target, float azimuthDeg, float elevationDeg, float distance,
                                     float fovYDeg, Camera::Projection projection) {
    camera_.setState(target, azimuthDeg, elevationDeg, distance, fovYDeg, projection);
    update();
}

geo::Ray ViewportWidget::makeRay(QPointF pos) const {
    const CameraRay ray = camera_.rayThrough(pos.x(), pos.y(), width(), height());
    return geo::Ray{toGeo(ray.origin), toGeo(ray.dir)};
}

geo::PickOptions ViewportWidget::tolerancesAt(double distance) const {
    const double pixel = camera_.worldPerPixel(distance, height());
    return geo::PickOptions{8.0 * pixel, 6.0 * pixel};
}

void ViewportWidget::setZoomAnchorResolver(std::function<std::optional<QVector3D>(const QPointF&)> resolver) {
    zoomAnchorResolver_ = std::move(resolver);
}

std::optional<QPointF> ViewportWidget::projectToScreen(const QVector3D& world) const {
    const QMatrix4x4 v1 = camera_.projectionMatrix(aspectRatio()) * camera_.viewMatrix();
    const QVector4D clip = v1 * QVector4D(world, 1.0f);
    if (clip.w() <= 1e-6f) return std::nullopt;  // behind the eye

    const float ndcX = clip.x() / clip.w();
    const float ndcY = clip.y() / clip.w();
    const float px = (ndcX * 0.5f + 0.5f) * static_cast<float>(width());
    const float py = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height());  // Qt pixel space
    return QPointF(px, py);
}

void ViewportWidget::uploadCueTraceIfActive() {
    if (!inferenceCue_ || !inferenceCue_->traceFrom) {
        cueTraceVertexCount_ = 0;
        return;
    }

    std::vector<float> dashVerts;
    buildDashedTrace(dashVerts, *inferenceCue_->traceFrom, inferenceCue_->pos,
                      camera_.worldPerPixel(camera_.distance(), height()));

    glBindBuffer(GL_ARRAY_BUFFER, vboCueTrace_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(dashVerts.size() * sizeof(float)), dashVerts.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    cueTraceVertexCount_ = static_cast<int>(dashVerts.size() / 3);
}

void ViewportWidget::paintInferenceCue(QPainter& painter, const InferenceCue& cue) const {
    const std::optional<QPointF> screenPos = projectToScreen(cue.pos);
    if (!screenPos) return;  // behind the camera -- nothing sane to anchor the overlay to
    const QPointF& c = *screenPos;

    // Marker glyph: skipped for a warning cue (red text, no marker) and for
    // kInferenceCueMarkerNone, where the rubber-band line carries the color.
    if (!cue.warning && cue.shape != kInferenceCueMarkerNone) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromRgbF(cue.r, cue.g, cue.b, cue.a));
        switch (cue.shape) {
            case kInferenceCueMarkerDiamond: {
                QPolygonF diamond;
                diamond << QPointF(c.x(), c.y() - kMarkerGlyphHalfExtentPx) << QPointF(c.x() + kMarkerGlyphHalfExtentPx, c.y())
                        << QPointF(c.x(), c.y() + kMarkerGlyphHalfExtentPx) << QPointF(c.x() - kMarkerGlyphHalfExtentPx, c.y());
                painter.drawPolygon(diamond);
                break;
            }
            case kInferenceCueMarkerSquare:
                painter.drawRect(QRectF(c.x() - kMarkerGlyphHalfExtentPx, c.y() - kMarkerGlyphHalfExtentPx,
                                         kMarkerGlyphHalfExtentPx * 2.0, kMarkerGlyphHalfExtentPx * 2.0));
                break;
            default:  // kInferenceCueMarkerDot
                painter.drawEllipse(c, kMarkerGlyphHalfExtentPx, kMarkerGlyphHalfExtentPx);
                break;
        }
    }

    if (cue.screenTip.isEmpty()) return;

    // ScreenTip: light-yellow box with black text, offset down-right of the
    // marker; a warning draws plain red text with no box.
    const QPointF textBaseline = c + QPointF(kScreenTipOffsetXPx, kScreenTipOffsetYPx);
    const QFontMetricsF metrics(painter.font());
    const QRectF textBounds = metrics.boundingRect(cue.screenTip);

    if (cue.warning) {
        painter.setPen(kScreenTipWarningColor);
        painter.drawText(textBaseline, cue.screenTip);
        return;
    }

    const QRectF box(textBaseline.x() - 3.0, textBaseline.y() - metrics.ascent() - 3.0, textBounds.width() + 6.0,
                      textBounds.height() + 6.0);
    painter.setPen(kScreenTipBorderColor);
    painter.setBrush(kScreenTipBgColor);
    painter.drawRect(box);
    painter.setPen(kScreenTipTextColor);
    painter.drawText(textBaseline, cue.screenTip);
}

void ViewportWidget::paintAnnotationLabels(QPainter& painter) const {
    for (const AnnotationLabel& label : pendingAnnotationLabels_) {
        if (label.text.isEmpty()) continue;

        // Anchor in widget pixels: screenFixed uses its position directly,
        // otherwise project worldPos and skip labels behind the eye.
        QPointF anchor;
        if (label.screenFixed) {
            anchor = label.screenPos;
        } else {
            const std::optional<QPointF> projected = projectToScreen(label.worldPos);
            if (!projected) continue;
            anchor = *projected;
        }

        const QFontMetricsF metrics(painter.font());
        const QRectF textBounds = metrics.boundingRect(label.text);

        if (!label.leader) {
            // Dimension label / screen text: no box or leader. Dimension
            // labels center horizontally on the anchor; screen text starts at
            // its fixed position.
            const QPointF baseline = label.screenFixed ? anchor + QPointF(0.0, metrics.ascent())
                                                        : anchor + QPointF(-textBounds.width() / 2.0, -4.0);
            painter.setPen(kAnnotationLabelTextColor);
            painter.drawText(baseline, label.text);
            continue;
        }

        // Leader text: anchor dot, a short screen-space leader line (the elbow
        // is a fixed pixel offset, not real geometry), then a box at the elbow.
        painter.setPen(Qt::NoPen);
        painter.setBrush(kAnnotationLabelBorderColor);
        painter.drawEllipse(anchor, kAnnotationAnchorDotRadiusPx, kAnnotationAnchorDotRadiusPx);

        const QPointF elbow = anchor + QPointF(kAnnotationLeaderOffsetXPx, kAnnotationLeaderOffsetYPx);
        painter.setPen(kAnnotationLabelBorderColor);
        painter.drawLine(anchor, elbow);

        const QPointF textBaseline = elbow + QPointF(4.0, metrics.ascent());
        const QRectF box(elbow.x() - 2.0, elbow.y() - 2.0, textBounds.width() + 8.0, textBounds.height() + 6.0);
        painter.setPen(kAnnotationLabelBorderColor);
        painter.setBrush(kAnnotationLabelBgColor);
        painter.drawRect(box);
        painter.setPen(kAnnotationLabelTextColor);
        painter.drawText(textBaseline, label.text);
    }
}

void ViewportWidget::uploadModelGeometryIfDirty() {
    if (!modelGeometryDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboModelEdges_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingEdgeVerts_.size() * sizeof(float)), pendingEdgeVerts_.data(),
                 GL_DYNAMIC_DRAW);
    modelEdgeVertexCount_ = static_cast<int>(pendingEdgeVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, vboFaces_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingFaceTris_.size() * sizeof(float)), pendingFaceTris_.data(),
                 GL_DYNAMIC_DRAW);
    // 8 floats/vertex (pos+uv+normal).
    faceVertexCount_ = static_cast<int>(pendingFaceTris_.size() / 8);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // transparentRanges_ is sorted back-to-front against the CURRENT eye
    // position once per rebuild. The widget sorts, not the presenter, because
    // camera_ already reflects an in-progress drag the CameraStore has not seen.
    opaqueRanges_ = std::move(pendingOpaqueRanges_);
    transparentRanges_ = std::move(pendingTransparentRanges_);
    materialSwatches_ = std::move(pendingMaterialSwatches_);
    orderTransparentRangesBackToFront(transparentRanges_, toGeo(camera_.eye()));

    modelGeometryDirty_ = false;
}

void ViewportWidget::uploadEdgeStyleGeometryIfDirty() {
    if (!edgeStyleGeometryDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboProfileEdges_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingProfileEdgeVerts_.size() * sizeof(float)),
                 pendingProfileEdgeVerts_.data(), GL_DYNAMIC_DRAW);
    profileEdgeVertexCount_ = static_cast<int>(pendingProfileEdgeVerts_.size() / 3);

    // Thick-quad counterpart, expanded from the same source and built
    // unconditionally; paintGL picks thin vs. thick at draw time.
    std::vector<float> profileThickVerts;
    appendThickLineSegments(profileThickVerts, pendingProfileEdgeVerts_);
    glBindBuffer(GL_ARRAY_BUFFER, vboProfileEdgesThick_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(profileThickVerts.size() * sizeof(float)),
                 profileThickVerts.data(), GL_DYNAMIC_DRAW);
    profileEdgeThickVertexCount_ = static_cast<int>(profileThickVerts.size() / 7);

    for (int i = 0; i < 3; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        const std::vector<float>& verts = pendingBandEdgeVerts_[idx];
        glBindBuffer(GL_ARRAY_BUFFER, vboBandEdges_[idx]);
        glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
        bandEdgeVertexCount_[idx] = static_cast<int>(verts.size() / 3);

        // Only bands 0/1 get a thick counterpart; band 2 stays plain 1px.
        if (i < 2) {
            std::vector<float> bandThickVerts;
            appendThickLineSegments(bandThickVerts, verts);
            glBindBuffer(GL_ARRAY_BUFFER, vboBandEdgesThick_[idx]);
            glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(bandThickVerts.size() * sizeof(float)),
                         bandThickVerts.data(), GL_DYNAMIC_DRAW);
            bandEdgeThickVertexCount_[idx] = static_cast<int>(bandThickVerts.size() / 7);
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    edgeStyleGeometryDirty_ = false;
}

QOpenGLTexture* ViewportWidget::textureFor(const std::string& hash, const QByteArray& bytes) {
    const auto it = textureCache_.find(hash);
    if (it != textureCache_.end()) return it->second.get();

    // A hash whose bytes never arrived, or bytes that don't decode. The miss
    // is deliberately NOT cached: a later call with real bytes must succeed.
    if (bytes.isEmpty()) return nullptr;
    const QImage image = QImage::fromData(bytes);
    if (image.isNull()) return nullptr;

    // flipped(): QImage row 0 is the TOP, but faceVertexUv's v is just a
    // world-space dot product with no "top of image" notion, so the flip is
    // what makes textures read right-side up.
    auto texture = std::make_unique<QOpenGLTexture>(image.flipped(Qt::Vertical));
    texture->setWrapMode(QOpenGLTexture::Repeat);
    texture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    texture->setMagnificationFilter(QOpenGLTexture::Linear);

    QOpenGLTexture* raw = texture.get();
    textureCache_.emplace(hash, std::move(texture));
    return raw;
}

void ViewportWidget::uploadDimmedGeometryIfDirty() {
    if (!dimmedGeometryDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboDimmedEdges_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingDimmedEdgeVerts_.size() * sizeof(float)),
                 pendingDimmedEdgeVerts_.data(), GL_DYNAMIC_DRAW);
    dimmedEdgeVertexCount_ = static_cast<int>(pendingDimmedEdgeVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, vboDimmedFaces_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingDimmedFaceTris_.size() * sizeof(float)),
                 pendingDimmedFaceTris_.data(), GL_DYNAMIC_DRAW);
    dimmedFaceVertexCount_ = static_cast<int>(pendingDimmedFaceTris_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    dimmedGeometryDirty_ = false;
}

void ViewportWidget::uploadGuideGeometryIfDirty() {
    if (!guideGeometryDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboGuides_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingGuideVerts_.size() * sizeof(float)),
                 pendingGuideVerts_.data(), GL_DYNAMIC_DRAW);
    guideVertexCount_ = static_cast<int>(pendingGuideVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    guideGeometryDirty_ = false;
}

void ViewportWidget::uploadAnnotationGeometryIfDirty() {
    if (!annotationGeometryDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboAnnotationLines_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingAnnotationLineVerts_.size() * sizeof(float)),
                 pendingAnnotationLineVerts_.data(), GL_DYNAMIC_DRAW);
    annotationLineVertexCount_ = static_cast<int>(pendingAnnotationLineVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, vboAnnotationLinesInvalid_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingAnnotationLineInvalidVerts_.size() * sizeof(float)),
                 pendingAnnotationLineInvalidVerts_.data(), GL_DYNAMIC_DRAW);
    annotationLineInvalidVertexCount_ = static_cast<int>(pendingAnnotationLineInvalidVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    annotationGeometryDirty_ = false;
}

void ViewportWidget::uploadSectionGeometryIfDirty() {
    if (!sectionGeometryDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboSectionActive_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingSectionActiveVerts_.size() * sizeof(float)),
                 pendingSectionActiveVerts_.data(), GL_DYNAMIC_DRAW);
    sectionActiveVertexCount_ = static_cast<int>(pendingSectionActiveVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, vboSectionInactive_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingSectionInactiveVerts_.size() * sizeof(float)),
                 pendingSectionInactiveVerts_.data(), GL_DYNAMIC_DRAW);
    sectionInactiveVertexCount_ = static_cast<int>(pendingSectionInactiveVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    sectionGeometryDirty_ = false;
}

void ViewportWidget::uploadPreviewIfDirty() {
    if (!previewDirty_) return;

    // Grow/shrink the GL-side pool to match pendingPreviewBatches_.
    while (previewBatches_.size() < pendingPreviewBatches_.size()) {
        PreviewBatchGL batch;
        glGenVertexArrays(1, &batch.vao);
        glGenBuffers(1, &batch.vbo);
        glBindVertexArray(batch.vao);
        glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
        previewBatches_.push_back(batch);
    }
    while (previewBatches_.size() > pendingPreviewBatches_.size()) {
        PreviewBatchGL& batch = previewBatches_.back();
        glDeleteVertexArrays(1, &batch.vao);
        glDeleteBuffers(1, &batch.vbo);
        previewBatches_.pop_back();
    }

    for (std::size_t i = 0; i < pendingPreviewBatches_.size(); ++i) {
        const PreviewBatch& src = pendingPreviewBatches_[i];
        PreviewBatchGL& dst = previewBatches_[i];
        glBindBuffer(GL_ARRAY_BUFFER, dst.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(src.lineVerts.size() * sizeof(float)), src.lineVerts.data(),
                     GL_DYNAMIC_DRAW);
        dst.vertexCount = static_cast<int>(src.lineVerts.size() / 3);
        dst.r = src.r;
        dst.g = src.g;
        dst.b = src.b;
        dst.a = src.a;
    }

    previewMarkerSet_ = pendingPreviewMarker_.has_value();
    glBindBuffer(GL_ARRAY_BUFFER, vboPreviewMarker_);
    if (previewMarkerSet_) {
        const QVector3D& marker = *pendingPreviewMarker_;
        const float markerVert[3] = {marker.x(), marker.y(), marker.z()};
        glBufferData(GL_ARRAY_BUFFER, sizeof(markerVert), markerVert, GL_DYNAMIC_DRAW);
    } else {
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    previewDirty_ = false;
}

void ViewportWidget::uploadSelectionIfDirty() {
    if (!selectionDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboSelectionEdges_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingSelectionEdgeVerts_.size() * sizeof(float)),
                 pendingSelectionEdgeVerts_.data(), GL_DYNAMIC_DRAW);
    selectionEdgeVertexCount_ = static_cast<int>(pendingSelectionEdgeVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, vboSelectionFaces_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingSelectionFaceTris_.size() * sizeof(float)),
                 pendingSelectionFaceTris_.data(), GL_DYNAMIC_DRAW);
    selectionFaceVertexCount_ = static_cast<int>(pendingSelectionFaceTris_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, vboSelectionPoints_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingSelectionPointVerts_.size() * sizeof(float)),
                 pendingSelectionPointVerts_.data(), GL_DYNAMIC_DRAW);
    selectionPointVertexCount_ = static_cast<int>(pendingSelectionPointVerts_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    selectionDirty_ = false;
}

void ViewportWidget::uploadHoverFaceIfDirty() {
    // Single-buffer lazy upload: this overlay has faces only, no edges/points.
    if (!hoverFaceDirty_) return;

    glBindBuffer(GL_ARRAY_BUFFER, vboHoverFaces_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<qsizetype>(pendingHoverFaceTris_.size() * sizeof(float)),
                 pendingHoverFaceTris_.data(), GL_DYNAMIC_DRAW);
    hoverFaceVertexCount_ = static_cast<int>(pendingHoverFaceTris_.size() / 3);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    hoverFaceDirty_ = false;
}

ViewportWidget::OverlayStats ViewportWidget::overlayStats() const {
    OverlayStats s;
    s.selectionEdgeVertexCount = selectionEdgeVertexCount_;
    s.selectionFaceVertexCount = selectionFaceVertexCount_;
    s.selectionPointVertexCount = selectionPointVertexCount_;
    s.pendingSelectionEdgeFloats = static_cast<int>(pendingSelectionEdgeVerts_.size());
    s.pendingSelectionFaceFloats = static_cast<int>(pendingSelectionFaceTris_.size());
    s.pendingSelectionPointFloats = static_cast<int>(pendingSelectionPointVerts_.size());
    s.selectionDirty = selectionDirty_;
    s.screenRectSet = screenRectSet_;
    s.paintCount = paintCount_;
    s.lastPaintSelectionEdgeVertexCount = lastPaintSelectionEdgeVertexCount_;
    for (std::size_t i = 0; i < s.firstSelectionEdgeVerts.size() && i < pendingSelectionEdgeVerts_.size(); ++i) {
        s.firstSelectionEdgeVerts[i] = pendingSelectionEdgeVerts_[i];
    }
    return s;
}

void ViewportWidget::uploadScreenRectIfDirty() {
    if (!screenRectDirty_) return;

    screenRectSet_ = pendingScreenRectA_.has_value() && pendingScreenRectB_.has_value();

    if (screenRectSet_) {
        // Widget pixels (Qt top-left origin) -> NDC, Y flipped. Drawn with an
        // identity MVP, so these ARE the final clip-space positions.
        const QPointF& a = *pendingScreenRectA_;
        const QPointF& b = *pendingScreenRectB_;
        const float w = static_cast<float>(width());
        const float h = static_cast<float>(height());
        const float ax = 2.0f * static_cast<float>(a.x()) / w - 1.0f;
        const float ay = 1.0f - 2.0f * static_cast<float>(a.y()) / h;
        const float bx = 2.0f * static_cast<float>(b.x()) / w - 1.0f;
        const float by = 1.0f - 2.0f * static_cast<float>(b.y()) / h;

        const float verts[12] = {
            ax, ay, 0.0f,
            bx, ay, 0.0f,
            bx, by, 0.0f,
            ax, by, 0.0f,
        };
        glBindBuffer(GL_ARRAY_BUFFER, vboScreenRect_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    screenRectDirty_ = false;
}

void ViewportWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    // AO targets follow the widget size whether or not AO is currently on.
    recreateAoTargets(w, h);
}

void ViewportWidget::recreateAoTargets(int w, int h) {
    w = std::max(w, 1);
    h = std::max(h, 1);
    const int halfW = std::max(w / kAoResolutionDivisor, 1);
    const int halfH = std::max(h / kAoResolutionDivisor, 1);

    // Delete whatever existed before; glDelete* ignores a 0 name.
    glDeleteFramebuffers(1, &aoGeomFbo_);
    glDeleteTextures(1, &aoDepthTex_);
    glDeleteTextures(1, &aoNormalTex_);
    glDeleteFramebuffers(1, &aoEstimateFbo_);
    glDeleteTextures(1, &aoRawTex_);
    glDeleteFramebuffers(1, &aoBlurFbo_);
    glDeleteTextures(1, &aoBlurTex_);

    // Pass A targets, FULL resolution, non-MSAA. Depth is GL_DEPTH_COMPONENT24
    // and NEAREST -- a blended depth at a silhouette edge would reconstruct a
    // bogus view position in Pass B. Normal is GL_RGB16F (components -1..1).
    glGenTextures(1, &aoDepthTex_);
    glBindTexture(GL_TEXTURE_2D, aoDepthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &aoNormalTex_);
    glBindTexture(GL_TEXTURE_2D, aoNormalTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &aoGeomFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, aoGeomFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoNormalTex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, aoDepthTex_, 0);

    // Pass B/C targets, HALF resolution, GL_R8. aoRawTex_ is NEAREST -- Pass
    // C's box blur is the one deliberate averaging step. aoBlurTex_ is LINEAR
    // because Pass D upsamples it to full res.
    glGenTextures(1, &aoRawTex_);
    glBindTexture(GL_TEXTURE_2D, aoRawTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, halfW, halfH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &aoEstimateFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, aoEstimateFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoRawTex_, 0);

    glGenTextures(1, &aoBlurTex_);
    glBindTexture(GL_TEXTURE_2D, aoBlurTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, halfW, halfH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &aoBlurFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, aoBlurFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, aoBlurTex_, 0);

    glBindTexture(GL_TEXTURE_2D, 0);
    // QOpenGLWidget's OWN framebuffer is NOT id 0 -- Qt composites the widget
    // into defaultFramebufferObject(), so every custom-FBO pass restores THAT id.
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());

    aoFullWidth_ = w;
    aoFullHeight_ = h;
    aoHalfWidth_ = halfW;
    aoHalfHeight_ = halfH;
}

void ViewportWidget::renderAmbientOcclusion(const QMatrix4x4& view, const QMatrix4x4& projection) {
    // Size self-check, run even when AO is off: the LIVE GL_VIEWPORT is ground
    // truth -- resizeGL's w/h is not the same size on every driver/DPI combination.
    {
        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] != aoFullWidth_ || vp[3] != aoFullHeight_) {
            recreateAoTargets(vp[2], vp[3]);
        }
    }

    if (!ambientOcclusion_ || aoGeomFbo_ == 0 || faceVertexCount_ <= 0) return;
    if (!aoGeomProgram_ || !aoGeomProgram_->isLinked() || !aoEstimateProgram_ || !aoEstimateProgram_->isLinked() ||
        !aoBlurProgram_ || !aoBlurProgram_->isLinked() || !aoCompositeProgram_ || !aoCompositeProgram_->isLinked()) {
        return;
    }

    const QMatrix4x4 invProjection = projection.inverted();
    const bool clipActive = clipPlane_.has_value();
    const QVector4D clipPlaneVec =
        clipActive ? QVector4D(clipPlane_->normal, QVector3D::dotProduct(clipPlane_->normal, clipPlane_->point))
                   : QVector4D();

    // -- Pass A: geometry (depth + view-space normal), FULL resolution ------
    // No color clear: aoNormalTex_ at a background pixel is never read. The
    // depth-only clear also avoids touching glClearColor, which is GLOBAL state.
    glBindFramebuffer(GL_FRAMEBUFFER, aoGeomFbo_);
    glViewport(0, 0, aoFullWidth_, aoFullHeight_);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);  // wholesale draw, both winding directions

    aoGeomProgram_->bind();
    aoGeomProgram_->setUniformValue("uView", view);
    aoGeomProgram_->setUniformValue("uProjection", projection);
    aoGeomProgram_->setUniformValue("uClipEnable", clipActive);
    if (clipActive) aoGeomProgram_->setUniformValue("uClipPlane", clipPlaneVec);
    glBindVertexArray(vaoFaces_);
    glDrawArrays(GL_TRIANGLES, 0, faceVertexCount_);
    glBindVertexArray(0);
    aoGeomProgram_->release();

    // -- Pass B: hemisphere-kernel occlusion estimate, HALF resolution ------
    // No depth attachment and no clear: depth testing is off for the rest of
    // this function, and each fullscreen triangle covers every target pixel.
    glBindFramebuffer(GL_FRAMEBUFFER, aoEstimateFbo_);
    glViewport(0, 0, aoHalfWidth_, aoHalfHeight_);
    glDisable(GL_DEPTH_TEST);

    aoEstimateProgram_->bind();
    aoEstimateProgram_->setUniformValue("uProjection", projection);
    aoEstimateProgram_->setUniformValue("uInvProjection", invProjection);
    aoEstimateProgram_->setUniformValue("uRadius", kAoSampleRadius);
    aoEstimateProgram_->setUniformValue("uBias", kAoBias);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, aoDepthTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, aoNormalTex_);
    glBindVertexArray(vaoFullscreenTri_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    aoEstimateProgram_->release();

    // -- Pass C: 4x4 box blur, HALF resolution -------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, aoBlurFbo_);
    // Viewport unchanged from Pass B (same half-resolution target).
    aoBlurProgram_->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, aoRawTex_);
    glBindVertexArray(vaoFullscreenTri_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    aoBlurProgram_->release();

    // -- Pass D: multiply-blend composite, FULL resolution, onto the widget's
    // default framebuffer (NOT literal id 0 -- see recreateAoTargets).
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, aoFullWidth_, aoFullHeight_);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);

    aoCompositeProgram_->bind();
    aoCompositeProgram_->setUniformValue("uAoStrength", aoStrength_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, aoBlurTex_);
    glBindVertexArray(vaoFullscreenTri_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    aoCompositeProgram_->release();

    // -- Full GL state restore ----------------------------------------------
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);

    // flatColorProgram_ left BOUND with uMvp restored -- every pass after this
    // call site in paintGL assumes it is already current.
    if (flatColorProgram_ && flatColorProgram_->isLinked()) {
        flatColorProgram_->bind();
        flatColorProgram_->setUniformValue("uMvp", projection * view);
    }
}

float ViewportWidget::aspectRatio() const {
    return height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
}

void ViewportWidget::paintGL() {
    // GL-state fingerprint at paint start, logged ONLY when it changes --
    // catches state leaked by a previous frame's passes without per-frame spam.
    {
        GLint frontFace = 0, depthFunc = 0, fbo = 0, viewportDims[4] = {0, 0, 0, 0};
        GLboolean depthMask = GL_FALSE;
        glGetIntegerv(GL_FRONT_FACE, &frontFace);
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, viewportDims);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        const QString fingerprint =
            QStringLiteral("glState: blend=%1 depthTest=%2 depthMask=%3 cull=%4 frontFace=%5 depthFunc=%6 "
                           "stencil=%7 fbo=%8(default=%9) vp=%10x%11 dpr=%12")
                .arg(glIsEnabled(GL_BLEND))
                .arg(glIsEnabled(GL_DEPTH_TEST))
                .arg(depthMask)
                .arg(glIsEnabled(GL_CULL_FACE))
                .arg(frontFace == GL_CCW ? QStringLiteral("CCW") : QStringLiteral("CW"))
                .arg(depthFunc, 0, 16)
                .arg(glIsEnabled(GL_STENCIL_TEST))
                .arg(fbo)
                .arg(defaultFramebufferObject())
                .arg(viewportDims[2])
                .arg(viewportDims[3])
                .arg(devicePixelRatioF());
        static QString lastFingerprint;
        if (fingerprint != lastFingerprint) {
            lastFingerprint = fingerprint;
            plnr::renderLog(fingerprint);
        }
    }

    // QPainter on a QOpenGLWidget leaves raw GL state changed (GL_DEPTH_TEST
    // off included), so re-establish the full baseline at the start of every
    // frame. The fingerprint log above samples BEFORE this, so leaks stay visible.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glFrontFace(GL_CCW);
    glClearColor(kBackgroundColor.x(), kBackgroundColor.y(), kBackgroundColor.z(), 1.0f);  // shared constant, keep in sync

    uploadAxesGeometryIfDirty();
    uploadModelGeometryIfDirty();
    uploadEdgeStyleGeometryIfDirty();
    uploadDimmedGeometryIfDirty();
    uploadGuideGeometryIfDirty();
    uploadAnnotationGeometryIfDirty();
    uploadSectionGeometryIfDirty();
    uploadPreviewIfDirty();
    uploadSelectionIfDirty();
    uploadHoverFaceIfDirty();
    uploadScreenRectIfDirty();

    ++paintCount_;
    lastPaintSelectionEdgeVertexCount_ = selectionEdgeVertexCount_;

    // GL_STENCIL_BUFFER_BIT: the ground-shadow pass's "darken once" technique
    // needs every pixel's stencil back at 0 each frame.
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // Sky backdrop, the frame's first draw, depth test and writes off -- so
    // cleared depth stays 1.0 and AO still reads sky pixels as background.
    // Parallel mode substitutes a same-fov PERSPECTIVE matrix, since parallel
    // rays would collapse the backdrop to one color.
    if (skyProgram_ && skyProgram_->isLinked()) {
        QMatrix4x4 skyProj;
        if (camera_.projection() == Camera::Projection::Parallel) {
            skyProj.perspective(camera_.fovYDeg(), aspectRatio(), 0.1f, 100.0f);
        } else {
            skyProj = camera_.projectionMatrix(aspectRatio());
        }
        const QMatrix4x4 skyInvVp = (skyProj * camera_.viewMatrix()).inverted();
        skyProgram_->bind();
        skyProgram_->setUniformValue("uInvVp", skyInvVp);
        skyProgram_->setUniformValue("uZenithColor", kSkyZenithColor);
        skyProgram_->setUniformValue("uHorizonColor", kSkyHorizonColor);
        skyProgram_->setUniformValue("uGroundColor", kBackgroundColor);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(vaoFullscreenTri_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        skyProgram_->release();
    }

    const QMatrix4x4 v1 = camera_.projectionMatrix(aspectRatio()) * camera_.viewMatrix();
    // World-space eye, fed to every fog-capable shader's uEyePos.
    const QVector3D eyePos = camera_.eye();

    // Ground grid + axes. Depth test ON (model geometry occludes it) but depth
    // WRITES off: the grid is coplanar at z=0, and writing depth would make the
    // polygon-offset face fill lose the test against it, striping the fill.
    if (program_ && program_->isLinked()) {
        program_->bind();
        program_->setUniformValue("uMvp", v1);

        glDepthMask(GL_FALSE);
        glBindVertexArray(vao_);
        glDrawArrays(GL_LINES, 0, vertexCount_);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);

        program_->release();
    }

    // Ground shadows: after the grid, before any model face/edge pass. Writes
    // no depth -- the flattened triangles sit at exactly z=0 and would z-fight.
    if (showShadows_ && sunDirection_.z() > kMinSunElevationForShadows && faceVertexCount_ > 0 && shadowProgram_ &&
        shadowProgram_->isLinked()) {
        const QMatrix4x4 shadowMvp = v1 * buildShadowMatrix(sunDirection_);

        // The stencil EQUAL-0/INCR pair lets a pixel's shadow composite EXACTLY
        // ONCE per frame, however many shadow triangles cover it. The frame's
        // GL_STENCIL_BUFFER_BIT clear resets it.
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        glStencilMask(0xFF);

        // Translucent overlay: depth test stays ON with WRITES off, so this
        // pass never blocks the opaque face fill that follows.
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        shadowProgram_->bind();
        shadowProgram_->setUniformValue("uMvp", shadowMvp);
        // Pure black, alpha from the Dark slider (0-100) -- a simplification;
        // real ground shadows often carry a color tint.
        shadowProgram_->setUniformValue("uColor", QVector4D(0.0f, 0.0f, 0.0f, shadowDark_ / 100.0f));
        // Reuses vaoFaces_ wholesale, unculled -- only the silhouette
        // footprint matters for a shadow.
        glDisable(GL_CULL_FACE);
        glBindVertexArray(vaoFaces_);
        glDrawArrays(GL_TRIANGLES, 0, faceVertexCount_);
        glBindVertexArray(0);
        shadowProgram_->release();

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDisable(GL_STENCIL_TEST);
    }

    if (!flatColorProgram_ || !flatColorProgram_->isLinked()) return;

    flatColorProgram_->bind();
    flatColorProgram_->setUniformValue("uMvp", v1);
    // Clipping starts disabled for every flatColorProgram_ draw this frame;
    // only the model faces/edges block turns it on, and off again right after.
    flatColorProgram_->setUniformValue("uClipEnable", false);
    // Sun shading: same scope -- only the face-fill block turns it on.
    flatColorProgram_->setUniformValue("uShadingEnabled", false);
    // Fog's scope is WIDER than shading's: on for every model pass below
    // (fills AND edges, even under Wireframe), off again for the overlays.
    flatColorProgram_->setUniformValue("uFogEnabled", false);

    // Editing-context dimmed overlay, drawn FIRST so it never overdraws
    // in-context geometry. Same polygon offset as model faces; one pass only.
    if (dimmedFaceVertexCount_ > 0) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        flatColorProgram_->setUniformValue("uColor", QVector4D(kDimmedFaceColor, 1.0f));
        glBindVertexArray(vaoDimmedFaces_);
        glDrawArrays(GL_TRIANGLES, 0, dimmedFaceVertexCount_);
        glBindVertexArray(0);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    if (dimmedEdgeVertexCount_ > 0) {
        flatColorProgram_->setUniformValue("uColor", QVector4D(kDimmedEdgeColor, 1.0f));
        glBindVertexArray(vaoDimmedEdges_);
        glDrawArrays(GL_LINES, 0, dimmedEdgeVertexCount_);
        glBindVertexArray(0);
    }

    // Fog on HERE, unconditionally: after the dimmed overlay (which stays
    // unfogged) but before the face-fill style gate, so Wireframe still gets
    // fogged edges. Each program sets its own copy of these uniforms.
    flatColorProgram_->setUniformValue("uFogEnabled", fogEnabled_);
    if (fogEnabled_) {
        flatColorProgram_->setUniformValue("uFogColor", fogColor_);
        flatColorProgram_->setUniformValue("uFogStart", fogStartDistance_);
        flatColorProgram_->setUniformValue("uFogEnd", fogEndDistance_);
        flatColorProgram_->setUniformValue("uEyePos", eyePos);
    }

    // Section-plane cut: model faces/edges only (this block and the
    // model-edges block after it), disabled again right after.
    if (clipPlane_) {
        flatColorProgram_->setUniformValue("uClipEnable", true);
        flatColorProgram_->setUniformValue(
            "uClipPlane", QVector4D(clipPlane_->normal, QVector3D::dotProduct(clipPlane_->normal, clipPlane_->point)));
    }

    // Face fills, pushed back by polygon offset so the model edges drawn right
    // after don't z-fight. Wireframe skips the block. One material range at a
    // time, two draw calls each with culling flipped so front/back resolve apart.
    if (style_ != FaceStyle::Wireframe && (!opaqueRanges_.empty() || !transparentRanges_.empty())) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
        glEnable(GL_CULL_FACE);

        glBindVertexArray(vaoFaces_);

        const bool clipActive = clipPlane_.has_value();
        const QVector4D clipPlaneVec =
            clipActive ? QVector4D(clipPlane_->normal, QVector3D::dotProduct(clipPlane_->normal, clipPlane_->point))
                       : QVector4D();

        // Sun shading applies in every style except HiddenLine (which
        // short-circuits in drawSide) and Wireframe (never reaches this block).
        const bool shadeFills = useSunForShading_ && style_ != FaceStyle::HiddenLine;
        // Set before the loop: bindFlat() re-sets this pair after every
        // texture draw, but the first range may draw without ever calling it.
        flatColorProgram_->setUniformValue("uShadingEnabled", shadeFills);
        if (shadeFills) {
            flatColorProgram_->setUniformValue("uSunDir", sunDirection_);
            flatColorProgram_->setUniformValue("uLight", shadowLight_);
        }

        // materialId 0 (unpainted, or an unresolved lookup) has no swatch.
        const auto resolveSwatch = [this](geo::Id materialId) -> const MaterialSwatch* {
            if (materialId == 0) return nullptr;
            const auto it = materialSwatches_.find(materialId);
            return it != materialSwatches_.end() ? &it->second : nullptr;
        };

        // Rebinds flatColorProgram_ with uMvp/uClip/uShading/uFog restored --
        // the state every other pass this frame assumes is already current.
        const auto bindFlat = [&]() {
            flatColorProgram_->bind();
            flatColorProgram_->setUniformValue("uMvp", v1);
            flatColorProgram_->setUniformValue("uClipEnable", clipActive);
            if (clipActive) flatColorProgram_->setUniformValue("uClipPlane", clipPlaneVec);
            flatColorProgram_->setUniformValue("uShadingEnabled", shadeFills);
            if (shadeFills) {
                flatColorProgram_->setUniformValue("uSunDir", sunDirection_);
                flatColorProgram_->setUniformValue("uLight", shadowLight_);
            }
            flatColorProgram_->setUniformValue("uFogEnabled", fogEnabled_);
            if (fogEnabled_) {
                flatColorProgram_->setUniformValue("uFogColor", fogColor_);
                flatColorProgram_->setUniformValue("uFogStart", fogStartDistance_);
                flatColorProgram_->setUniformValue("uFogEnd", fogEndDistance_);
                flatColorProgram_->setUniformValue("uEyePos", eyePos);
            }
        };

        // Per-style rules for drawSide: Monochrome ignores materials entirely;
        // ShadedWithTextures/XRay bind textures where plain Shaded shows only
        // material RGB; HiddenLine short-circuits; XRay scales every alpha.
        const bool useMaterials = style_ != FaceStyle::Monochrome;
        const bool useTextures = style_ == FaceStyle::ShadedWithTextures || style_ == FaceStyle::XRay;
        const float alphaMultiplier = style_ == FaceStyle::XRay ? kXRayAlpha : 1.0f;

        // Draws range's [first,count) slice per the active style.
        // texturedProgram_ is used only when useTextures and the swatch's
        // texture loads, and only for its own draw call -- this lambda always
        // returns with flatColorProgram_ live.
        const auto drawSide = [&](geo::Id materialId, const QVector3D& fallbackColor, const MaterialRange& range) {
            if (style_ == FaceStyle::HiddenLine) {
                flatColorProgram_->setUniformValue("uColor", QVector4D(kHiddenLineFillColor, 1.0f));
                glDrawArrays(GL_TRIANGLES, range.first, range.count);
                return;
            }

            const MaterialSwatch* swatch = useMaterials ? resolveSwatch(materialId) : nullptr;
            QOpenGLTexture* tex = (useTextures && swatch && !swatch->assetHash.empty() && texturedProgram_ &&
                                    texturedProgram_->isLinked())
                                       ? textureFor(swatch->assetHash, swatch->textureBytes)
                                       : nullptr;
            if (tex) {
                flatColorProgram_->release();
                texturedProgram_->bind();
                texturedProgram_->setUniformValue("uMvp", v1);
                texturedProgram_->setUniformValue("uClipEnable", clipActive);
                if (clipActive) texturedProgram_->setUniformValue("uClipPlane", clipPlaneVec);
                texturedProgram_->setUniformValue("uOpacity", swatch->opacity * alphaMultiplier);
                texturedProgram_->setUniformValue("uShadingEnabled", shadeFills);
                if (shadeFills) {
                    texturedProgram_->setUniformValue("uSunDir", sunDirection_);
                    texturedProgram_->setUniformValue("uLight", shadowLight_);
                }
                texturedProgram_->setUniformValue("uFogEnabled", fogEnabled_);
                if (fogEnabled_) {
                    texturedProgram_->setUniformValue("uFogColor", fogColor_);
                    texturedProgram_->setUniformValue("uFogStart", fogStartDistance_);
                    texturedProgram_->setUniformValue("uFogEnd", fogEndDistance_);
                    texturedProgram_->setUniformValue("uEyePos", eyePos);
                }
                tex->bind();
                glDrawArrays(GL_TRIANGLES, range.first, range.count);
                tex->release();
                texturedProgram_->release();
                bindFlat();
                return;
            }
            const float baseAlpha = swatch ? static_cast<float>(swatch->opacity) : 1.0f;
            const QVector3D rgb = swatch ? swatch->rgb : fallbackColor;
            flatColorProgram_->setUniformValue("uColor", QVector4D(rgb, baseAlpha * alphaMultiplier));
            glDrawArrays(GL_TRIANGLES, range.first, range.count);
        };

        // fallbackColor is styleFrontColor_/styleBackColor_: the unpainted-face
        // fallback, and Monochrome's unconditional color.
        const auto drawRange = [&](const MaterialRange& range) {
            glCullFace(GL_BACK);
            drawSide(range.frontMaterialId, styleFrontColor_, range);

            glCullFace(GL_FRONT);
            drawSide(range.backMaterialId, styleBackColor_, range);
        };

        if (style_ == FaceStyle::HiddenLine || style_ == FaceStyle::Monochrome) {
            // Both styles fill fully opaque regardless of material, so the
            // presenter's opaque/transparent partition is ignored here.
            for (const MaterialRange& range : opaqueRanges_) drawRange(range);
            for (const MaterialRange& range : transparentRanges_) drawRange(range);
        } else if (style_ == FaceStyle::XRay) {
            // X-Ray makes everything translucent, again ignoring the
            // partition: opaqueRanges_ draws before the already-sorted
            // transparentRanges_ rather than a combined depth sort.
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            for (const MaterialRange& range : opaqueRanges_) drawRange(range);
            for (const MaterialRange& range : transparentRanges_) drawRange(range);
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        } else {
            // Shaded/ShadedWithTextures: opaque ranges first (any order),
            // then transparent ones back-to-front under the blend bracket.
            for (const MaterialRange& range : opaqueRanges_) drawRange(range);

            if (!transparentRanges_.empty()) {
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                for (const MaterialRange& range : transparentRanges_) drawRange(range);
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
        }

        glBindVertexArray(0);
        glDisable(GL_CULL_FACE);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // Sun shading is scoped to the face-fill block -- off again here
    // unconditionally, since nothing below shades.
    flatColorProgram_->setUniformValue("uShadingEnabled", false);

    // Model edges, black, at true depth: the profile bucket and the three
    // bands together cover every edge exactly once. profiles_/depthCue_ switch
    // the profile bucket and bands 0-1 to thickLineProgram_'s quads (band 2 has
    // none); flat is rebound after, since every pass below assumes it is current.
    flatColorProgram_->setUniformValue("uColor", QVector4D(kModelEdgeColor, 1.0f));

    for (int i = 0; i < 3; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        const bool bandIsThickThisFrame = depthCue_ && i < 2;
        if (bandIsThickThisFrame || bandEdgeVertexCount_[idx] == 0) continue;
        glBindVertexArray(vaoBandEdges_[idx]);
        glDrawArrays(GL_LINES, 0, bandEdgeVertexCount_[idx]);
        glBindVertexArray(0);
    }
    if (!profiles_ && profileEdgeVertexCount_ > 0) {
        glBindVertexArray(vaoProfileEdges_);
        glDrawArrays(GL_LINES, 0, profileEdgeVertexCount_);
        glBindVertexArray(0);
    }

    if ((depthCue_ || profiles_) && thickLineProgram_ && thickLineProgram_->isLinked()) {
        thickLineProgram_->bind();
        thickLineProgram_->setUniformValue("uMvp", v1);
        thickLineProgram_->setUniformValue("uViewportSize",
                                            QVector2D(static_cast<float>(width()), static_cast<float>(height())));
        thickLineProgram_->setUniformValue("uColor", QVector4D(kModelEdgeColor, 1.0f));
        thickLineProgram_->setUniformValue("uClipEnable", clipPlane_.has_value());
        if (clipPlane_) {
            thickLineProgram_->setUniformValue(
                "uClipPlane",
                QVector4D(clipPlane_->normal, QVector3D::dotProduct(clipPlane_->normal, clipPlane_->point)));
        }
        // A separate program object, so it needs its own copy of the fog
        // uniforms.
        thickLineProgram_->setUniformValue("uFogEnabled", fogEnabled_);
        if (fogEnabled_) {
            thickLineProgram_->setUniformValue("uFogColor", fogColor_);
            thickLineProgram_->setUniformValue("uFogStart", fogStartDistance_);
            thickLineProgram_->setUniformValue("uFogEnd", fogEndDistance_);
            thickLineProgram_->setUniformValue("uEyePos", eyePos);
        }

        if (depthCue_) {
            for (int i = 0; i < 2; ++i) {
                const std::size_t idx = static_cast<std::size_t>(i);
                if (bandEdgeThickVertexCount_[idx] == 0) continue;
                thickLineProgram_->setUniformValue("uHalfWidthPx", kEdgeDepthBandWidths[idx] * 0.5f);
                glBindVertexArray(vaoBandEdgesThick_[idx]);
                glDrawArrays(GL_TRIANGLES, 0, bandEdgeThickVertexCount_[idx]);
                glBindVertexArray(0);
            }
        }
        if (profiles_ && profileEdgeThickVertexCount_ > 0) {
            thickLineProgram_->setUniformValue("uHalfWidthPx", kProfileEdgeWidth * 0.5f);
            glBindVertexArray(vaoProfileEdgesThick_);
            glDrawArrays(GL_TRIANGLES, 0, profileEdgeThickVertexCount_);
            glBindVertexArray(0);
        }

        flatColorProgram_->bind();
        flatColorProgram_->setUniformValue("uMvp", v1);
        flatColorProgram_->setUniformValue("uClipEnable", clipPlane_.has_value());
        if (clipPlane_) {
            flatColorProgram_->setUniformValue(
                "uClipPlane",
                QVector4D(clipPlane_->normal, QVector3D::dotProduct(clipPlane_->normal, clipPlane_->point)));
        }
    }

    // Back edges: an extra pass over ALL model edges (the unclassified buffer)
    // with the depth test INVERTED (GL_GREATER, so only normally-hidden edges
    // pass) and writes off. State fully restored after; clipPlane_ is ignored.
    if (backEdges_ && modelEdgeVertexCount_ > 0 && backEdgeProgram_ && backEdgeProgram_->isLinked()) {
        backEdgeProgram_->bind();
        backEdgeProgram_->setUniformValue("uMvp", v1);
        backEdgeProgram_->setUniformValue("uColor", QVector4D(kBackEdgeColor, 1.0f));
        // Separate program object -- its own copy of the fog uniforms.
        backEdgeProgram_->setUniformValue("uFogEnabled", fogEnabled_);
        if (fogEnabled_) {
            backEdgeProgram_->setUniformValue("uFogColor", fogColor_);
            backEdgeProgram_->setUniformValue("uFogStart", fogStartDistance_);
            backEdgeProgram_->setUniformValue("uFogEnd", fogEndDistance_);
            backEdgeProgram_->setUniformValue("uEyePos", eyePos);
        }
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        glBindVertexArray(vaoModelEdges_);
        glDrawArrays(GL_LINES, 0, modelEdgeVertexCount_);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        backEdgeProgram_->release();

        flatColorProgram_->bind();
        flatColorProgram_->setUniformValue("uMvp", v1);
    }

    // Ambient occlusion composited HERE: after every model face/edge/back-edge
    // pass (AO darkens those) but before any overlay pass (which must not be
    // darkened). Returns with flatColorProgram_ bound and uMvp restored.
    renderAmbientOcclusion(camera_.viewMatrix(), camera_.projectionMatrix(aspectRatio()));

    // Clipping is model faces/edges only -- off again here so the overlay
    // passes below render unclipped.
    if (clipPlane_) {
        flatColorProgram_->setUniformValue("uClipEnable", false);
    }
    // Fog has the same scope as clipping above -- off again here so the
    // overlay passes render unfogged.
    flatColorProgram_->setUniformValue("uFogEnabled", false);

    // Guide lines/points: one pre-baked GL_LINES buffer, drawn at true depth.
    // Simplification: the reference modeler draws guides THROUGH solid geometry.
    if (guideVertexCount_ > 0) {
        flatColorProgram_->setUniformValue("uColor", QVector4D(kGuideColor, 1.0f));
        glBindVertexArray(vaoGuides_);
        glDrawArrays(GL_LINES, 0, guideVertexCount_);
        glBindVertexArray(0);
    }

    // Annotation geometry: extension/dimension/tick lines, at true depth.
    if (annotationLineVertexCount_ > 0) {
        flatColorProgram_->setUniformValue("uColor", QVector4D(kAnnotationColor, 1.0f));
        glBindVertexArray(vaoAnnotationLines_);
        glDrawArrays(GL_LINES, 0, annotationLineVertexCount_);
        glBindVertexArray(0);
    }
    if (annotationLineInvalidVertexCount_ > 0) {
        flatColorProgram_->setUniformValue("uColor", QVector4D(kAnnotationInvalidColor, 1.0f));
        glBindVertexArray(vaoAnnotationLinesInvalid_);
        glDrawArrays(GL_LINES, 0, annotationLineInvalidVertexCount_);
        glBindVertexArray(0);
    }

    // Selection highlight (face stipple, edges, points) coincides exactly in
    // depth with the model it highlights, so the whole block -- hover face
    // included -- runs under GL_LEQUAL and restores GL_LESS right after.
    const bool selectionActive = selectionFaceVertexCount_ > 0 || selectionEdgeVertexCount_ > 0 ||
                                 selectionPointVertexCount_ > 0 || hoverFaceVertexCount_ > 0;
    if (selectionActive) glDepthFunc(GL_LEQUAL);

    // Selected-face and hover-face stipple both draw through stippleProgram_
    // in kSelectionColor; being its own program, its shading/fog/clip
    // uniforms are set off explicitly.
    if ((selectionFaceVertexCount_ > 0 || hoverFaceVertexCount_ > 0) && stippleProgram_ && stippleProgram_->isLinked()) {
        // Polygon-offset the stipple toward the camera, mirroring the model
        // faces' +1/+1, so it wins the now-LEQUAL test against the face beneath.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
        stippleProgram_->bind();
        stippleProgram_->setUniformValue("uMvp", v1);
        stippleProgram_->setUniformValue("uColor", QVector4D(kSelectionColor, 1.0f));
        stippleProgram_->setUniformValue("uClipEnable", false);
        stippleProgram_->setUniformValue("uShadingEnabled", false);
        stippleProgram_->setUniformValue("uFogEnabled", false);
        if (selectionFaceVertexCount_ > 0) {
            glBindVertexArray(vaoSelectionFaces_);
            glDrawArrays(GL_TRIANGLES, 0, selectionFaceVertexCount_);
        }
        if (hoverFaceVertexCount_ > 0) {
            glBindVertexArray(vaoHoverFaces_);
            glDrawArrays(GL_TRIANGLES, 0, hoverFaceVertexCount_);
        }
        glBindVertexArray(0);
        stippleProgram_->release();
        flatColorProgram_->bind();  // the selection-edge pass below expects flat still bound
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    if (selectionEdgeVertexCount_ > 0) {
        flatColorProgram_->setUniformValue("uColor", QVector4D(kSelectionColor, 1.0f));
        glBindVertexArray(vaoSelectionEdges_);
        glDrawArrays(GL_LINES, 0, selectionEdgeVertexCount_);
        glBindVertexArray(0);
    }

    flatColorProgram_->release();

    // Selection vertex markers: separate program (core profile needs
    // gl_PointSize written). Still under the GL_LEQUAL loosening above.
    if (selectionPointVertexCount_ > 0 && pointProgram_ && pointProgram_->isLinked()) {
        pointProgram_->bind();
        pointProgram_->setUniformValue("uMvp", v1);
        pointProgram_->setUniformValue("uColor", kSelectionColor);
        pointProgram_->setUniformValue("uPointSize", kSelectionPointSize);
        glBindVertexArray(vaoSelectionPoints_);
        glDrawArrays(GL_POINTS, 0, selectionPointVertexCount_);
        glBindVertexArray(0);
        pointProgram_->release();
    }

    if (selectionActive) glDepthFunc(GL_LESS);

    // Section-plane overlay: alpha-blended outlines via previewLineProgram_,
    // drawn just before the tool previews so a live preview lands on top.
    if ((sectionActiveVertexCount_ > 0 || sectionInactiveVertexCount_ > 0) && previewLineProgram_ &&
        previewLineProgram_->isLinked()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        previewLineProgram_->bind();
        previewLineProgram_->setUniformValue("uMvp", v1);
        if (sectionInactiveVertexCount_ > 0) {
            previewLineProgram_->setUniformValue("uColor", QVector4D(kSectionColor, kSectionInactiveAlpha));
            glBindVertexArray(vaoSectionInactive_);
            glDrawArrays(GL_LINES, 0, sectionInactiveVertexCount_);
            glBindVertexArray(0);
        }
        if (sectionActiveVertexCount_ > 0) {
            previewLineProgram_->setUniformValue("uColor", QVector4D(kSectionColor, kSectionActiveAlpha));
            glBindVertexArray(vaoSectionActive_);
            glDrawArrays(GL_LINES, 0, sectionActiveVertexCount_);
            glBindVertexArray(0);
        }
        previewLineProgram_->release();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    // Preview batches, one draw call each (own RGBA). Depth test on, writes
    // off, so the preview never blocks anything drawn after.
    if (!previewBatches_.empty() && previewLineProgram_ && previewLineProgram_->isLinked()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        previewLineProgram_->bind();
        previewLineProgram_->setUniformValue("uMvp", v1);
        for (const PreviewBatchGL& batch : previewBatches_) {
            if (batch.vertexCount <= 0) continue;
            previewLineProgram_->setUniformValue("uColor", QVector4D(batch.r, batch.g, batch.b, batch.a));
            glBindVertexArray(batch.vao);
            glDrawArrays(GL_LINES, 0, batch.vertexCount);
            glBindVertexArray(0);
        }
        previewLineProgram_->release();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    // Preview snap marker: separate program, same depth-on/write-off treatment.
    if (previewMarkerSet_ && pointProgram_ && pointProgram_->isLinked()) {
        glDepthMask(GL_FALSE);
        pointProgram_->bind();
        pointProgram_->setUniformValue("uMvp", v1);
        pointProgram_->setUniformValue("uColor", kPreviewMarkerColor);
        pointProgram_->setUniformValue("uPointSize", kPreviewMarkerPointSize);
        glBindVertexArray(vaoPreviewMarker_);
        glDrawArrays(GL_POINTS, 0, 1);
        glBindVertexArray(0);
        pointProgram_->release();
        glDepthMask(GL_TRUE);
    }

    // Drag-selection rubber band: last of the raw GL passes, identity MVP
    // (verts are already clip-space), depth testing off so it sits on top.
    if (screenRectSet_ && flatColorProgram_ && flatColorProgram_->isLinked()) {
        glDisable(GL_DEPTH_TEST);
        flatColorProgram_->bind();
        flatColorProgram_->setUniformValue("uMvp", QMatrix4x4());  // identity
        flatColorProgram_->setUniformValue("uColor", QVector4D(kScreenRectColor, 1.0f));
        glBindVertexArray(vaoScreenRect_);
        glDrawArrays(GL_LINE_LOOP, 0, 4);
        glBindVertexArray(0);
        flatColorProgram_->release();
        glEnable(GL_DEPTH_TEST);
    }

    // Inference cue: the dashed world-space trace line first, then the
    // QPainter marker + ScreenTip. Constructing QPainter(this) at paintGL's
    // tail, after every raw GL call, is Qt's documented pattern.
    uploadCueTraceIfActive();
    if (cueTraceVertexCount_ > 0 && previewLineProgram_ && previewLineProgram_->isLinked()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        previewLineProgram_->bind();
        previewLineProgram_->setUniformValue("uMvp", v1);
        previewLineProgram_->setUniformValue(
            "uColor", QVector4D(inferenceCue_->tr, inferenceCue_->tg, inferenceCue_->tb, inferenceCue_->ta));
        glBindVertexArray(vaoCueTrace_);
        glDrawArrays(GL_LINES, 0, cueTraceVertexCount_);
        glBindVertexArray(0);
        previewLineProgram_->release();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    // Annotation text labels: QPainter, drawn before the inference cue so a
    // live cue lands on top.
    if (!pendingAnnotationLabels_.empty() || inferenceCue_) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!pendingAnnotationLabels_.empty()) paintAnnotationLabels(painter);
        if (inferenceCue_) paintInferenceCue(painter, *inferenceCue_);
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        lastMousePos_ = event->pos();
        if (event->modifiers() & Qt::ShiftModifier) {
            panning_ = true;
        } else {
            orbiting_ = true;
        }
        return;
    }

    // Right button opens the context menu instead of reaching a tool;
    // ToolController builds and exec()s it synchronously.
    if (event->button() == Qt::RightButton) {
        emit contextMenuRequested(event->position());
        QOpenGLWidget::mousePressEvent(event);
        return;
    }

    // Only the left button reaches tools; middle navigates, right opens the menu.
    if (event->button() == Qt::LeftButton) {
        emit pointerPressed(event->position(), event->button(), event->modifiers());
    }
    QOpenGLWidget::mousePressEvent(event);
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    // Qt delivers the second press of a double-click here instead of a second
    // mousePressEvent -- treat it identically so clickCount synthesis works.
    if (event->button() == Qt::MiddleButton) {
        lastMousePos_ = event->pos();
        if (event->modifiers() & Qt::ShiftModifier) {
            panning_ = true;
        } else {
            orbiting_ = true;
        }
        return;
    }

    if (event->button() == Qt::RightButton) {
        emit contextMenuRequested(event->position());
        QOpenGLWidget::mouseDoubleClickEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        emit pointerPressed(event->position(), event->button(), event->modifiers());
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    if (orbiting_ || panning_) {
        const QPoint delta = event->pos() - lastMousePos_;
        lastMousePos_ = event->pos();

        if (orbiting_) {
            // Drag right rotates the scene naturally (azimuth decreases);
            // drag up looks down on the model more, matching the reference modeler feel.
            camera_.orbit(-delta.x() * 0.4f, delta.y() * 0.4f);
        } else {
            camera_.pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()), static_cast<float>(height()));
        }
        update();
        // A navigation drag is in progress: swallow the move, don't forward it.
        return;
    }

    // Not navigating: forward every move, hover or left-drag -- tools need
    // both for continuous feedback.
    emit pointerMoved(event->position(), event->modifiers());
    QOpenGLWidget::mouseMoveEvent(event);
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        // A navigation gesture just ended -- mirror the settled camera out.
        // Guarded on orbiting_/panning_ so a stray release emits nothing.
        const bool wasNavigating = orbiting_ || panning_;
        orbiting_ = false;
        panning_ = false;
        if (wasNavigating) {
            emit cameraNavigated(camera_.target(), camera_.azimuthDeg(), camera_.elevationDeg(), camera_.distance(),
                                  camera_.fovYDeg());
        }
        return;
    }

    if (event->button() == Qt::LeftButton) {
        emit pointerReleased(event->position(), event->button(), event->modifiers());
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;

    // The wheel zoom anchors on the cursor, unlike the Zoom TOOL's
    // center-of-screen drag. event->position() is the widget-pixel space
    // zoomAnchorResolver_ expects; unset or declining falls back to center zoom.
    const std::optional<QVector3D> anchor = zoomAnchorResolver_ ? zoomAnchorResolver_(event->position()) : std::nullopt;
    if (anchor) {
        camera_.zoomToward(*anchor, steps);
    } else {
        camera_.zoom(steps);
    }
    update();
    // Each wheel step is its own complete gesture, unlike an orbit/pan drag.
    emit cameraNavigated(camera_.target(), camera_.azimuthDeg(), camera_.elevationDeg(), camera_.distance(),
                          camera_.fovYDeg());
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Z && (event->modifiers() & Qt::ShiftModifier)) {
        camera_.zoomExtents(kGridBoundsMin, kGridBoundsMax, aspectRatio());
        update();
        return;
    }

    // Key-repeat is suppressed app-wide: LineTool's Alt/arrow toggles are
    // edge-triggered and must fire once per physical press.
    if (event->isAutoRepeat()) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    // Everything else -- Escape, digits, Enter -- is forwarded to tools.
    emit keyPressed(event->key(), event->modifiers());
    QOpenGLWidget::keyPressEvent(event);
}

}  // namespace plnr::viewport
