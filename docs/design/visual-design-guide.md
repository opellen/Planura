# Planura Visual Design Guide

## 1. Brand Concept

**Planura** is a 3D spatial modeling application built around the concept:

> **From Plan to Space**

The visual identity should express the transition from a **2D architectural plan** into a **3D spatial form**.

The core visual language combines:

- architectural plans
- geometric planes
- extrusion
- isometric projection
- negative space
- precise CAD-like construction
- minimal modern software branding

Planura should feel closer to a **professional architecture/design tool** than to a generic 3D engine or engineering utility.

---

# 2. Core Logo Concept

The primary Planura symbol is based on a stylized **capital `P`**.

The `P` should not simply be drawn as typography.

Instead, it should emerge naturally from architectural geometry:

1. A flat **2D plan** exists on the ground plane.
2. Part of that geometry rises vertically.
3. The vertical and upper planes form a recognizable `P`.
4. Negative space creates the inner counter of the `P`.
5. The result represents a transition from **Plan → Space**.

Conceptually:

    PLAN
      ↓
    EXTRUDE
      ↓
    SPACE

The logo should still work even when the viewer does not immediately recognize the letter `P`.

The first impression should be:

> geometric architectural object

The second impression should be:

> letter P

The third interpretation should be:

> 2D plan becoming a 3D structure

---

# 3. Visual Character

Planura graphics should use the following characteristics.

- Minimal
- Geometric
- Architectural
- Precise
- Calm
- Modern
- Professional
- Spatial
- Slightly futuristic
- Strong negative space

Avoid decorative complexity.

Prefer:

    3–6 geometric surfaces

instead of:

    many small details

Every shape should appear intentional.

---

# 4. Primary Color Palette

## Primary Indigo

Main Planura brand color.

```css
--planura-indigo: #3039C9;
```

Use for:

- primary surfaces
- major icon geometry
- brand accents
- active UI elements

---

## Bright Indigo

Used primarily for illuminated or upper-facing surfaces.

```css
--planura-indigo-bright: #4050F0;
```

Use for:

- top-facing planes
- highlighted surfaces
- subtle dimensional contrast

---

## Deep Indigo

Used for darker structural surfaces.

```css
--planura-indigo-deep: #2631A8;
```

Use for:

- vertical faces
- shadow-facing geometry
- strong structural elements

---

## Blueprint Blue

Used for outlines and architectural plan geometry.

```css
--planura-blueprint: #293AB4;
```

Use for:

- floor-plan outlines
- technical construction lines
- icon strokes
- secondary geometric elements

---

## Pale Spatial Blue

Used for secondary or receding surfaces.

```css
--planura-spatial-light: #D0D8FA;
```

Use for:

- interior faces
- distant surfaces
- secondary planes

---

## Spatial Mid Blue

```css
--planura-spatial-mid: #9EAFE9;
```

Use as the darker end of light spatial surfaces.

---

## Floor / Construction Light

```css
--planura-floor-light: #E5E9FC;
--planura-floor-mid:   #C4CCEF;
```

Use sparingly for:

- projected surfaces
- floor planes
- subtle construction geometry

---

## Background

Preferred background:

```css
--planura-background: #FFFFFF;
```

Alternative slightly warm/off-white UI background:

```css
--planura-background-soft: #FAFAFC;
```

The logo should retain generous white space.

---

# 5. Recommended Gradient System

Gradients should represent **surface orientation**, not decoration.

They must remain subtle.

## Top-facing plane

```svg
<linearGradient id="planuraTop" x1="0%" y1="0%" x2="100%" y2="100%">
    <stop offset="0%" stop-color="#4050F0"/>
    <stop offset="100%" stop-color="#3039C9"/>
</linearGradient>
```

---

## Dark vertical plane

```svg
<linearGradient id="planuraSide" x1="0%" y1="0%" x2="100%" y2="100%">
    <stop offset="0%" stop-color="#2D39BD"/>
    <stop offset="100%" stop-color="#2631A8"/>
</linearGradient>
```

---

## Light spatial plane

```svg
<linearGradient id="planuraLightFace" x1="0%" y1="0%" x2="100%" y2="100%">
    <stop offset="0%" stop-color="#D0D8FA"/>
    <stop offset="100%" stop-color="#9EAFE9"/>
</linearGradient>
```

---

# 6. Surface Color Logic

Use color according to the direction of each 3D surface.

### Top-facing surface

Brightest saturated blue.

```text
#4050F0 → #3039C9
```

### Main vertical surface

Deep indigo.

```text
#2D39BD → #2631A8
```

### Secondary / inner surface

Pale blue.

```text
#D0D8FA → #9EAFE9
```

### Plan / wireframe

Blueprint blue.

```text
#293AB4
```

This orientation-based color system should also be used for other Planura icons.

---

# 7. Geometry Language

## Isometric Perspective

Prefer isometric or pseudo-isometric geometry.

Typical directions:

```text
vertical:   90°
left axis:  ~150°
right axis: ~30°
```

Exact mathematical isometric projection is not mandatory.

Visual balance is more important than mathematical precision.

---

## Planes

Use large, clean polygons.

Preferred:

```text
polygon
polyline
path with straight segments
```

Avoid unnecessary Bézier curves unless the tool itself represents:

- arcs
- circles
- rotation
- curved surfaces

Architectural tools should generally look structurally geometric.

---

# 8. Line Style

Planura icon outlines should be clean and relatively strong.

Recommended SVG stroke characteristics:

```svg
stroke="#293AB4"
stroke-linecap="butt"
stroke-linejoin="miter"
```

For softer utility icons:

```svg
stroke-linecap="round"
stroke-linejoin="round"
```

Recommended relative stroke width:

```text
1.5% – 2.5% of icon width
```

For a `512 × 512` icon:

```text
8px – 12px
```

For a `1024 × 1024` icon:

```text
16px – 24px
```

---

# 9. Blueprint / Construction Lines

Some icons may contain architectural construction guides.

Use sparingly.

Example:

```svg
stroke="#9EAFE9"
stroke-width="4"
stroke-dasharray="10 12"
fill="none"
```

Construction lines should always have lower visual priority than the main object.

Typical hierarchy:

```text
3D object
    ↓
plan outline
    ↓
construction guide
```

Never allow blueprint details to dominate the icon.

---

# 10. Negative Space

Negative space is an important part of the Planura identity.

Prefer creating openings through empty space rather than drawing additional shapes.

Examples:

- doorway
- room opening
- inner `P`
- cut face
- architectural void
- extrusion opening

The logo's internal white area is intentional and should remain visually clean.

---

# 11. Icon Composition

Each Planura tool icon should ideally contain:

```text
one primary object
+
one action or transformation
```

Examples:

```text
Rectangle
→ planar quadrilateral

Push/Pull
→ plane + extrusion

Move
→ object + directional displacement

Rotate
→ object + curved rotational indicator

Orbit
→ spatial object + orbit arc

Line
→ two points connected by one precise line
```

Do not represent every feature of a tool.

Communicate the **core operation**.

---

# 12. Icon Silhouette

Icons should remain recognizable at:

```text
16 × 16
24 × 24
32 × 32
48 × 48
64 × 64
```

Therefore:

- use large shapes
- minimize small internal details
- maintain strong negative space
- avoid thin decorative lines
- avoid text inside icons

A good Planura icon should still be understandable when converted to a monochrome silhouette.

---

# 13. SVG Structure

Prefer clean SVG markup.

Recommended:

```svg
<svg
    xmlns="http://www.w3.org/2000/svg"
    viewBox="0 0 512 512">

    <defs>
        <!-- Planura gradients -->
    </defs>

    <!-- Primary geometry -->

    <!-- Secondary geometry -->

    <!-- Optional construction lines -->

</svg>
```

Use:

```text
viewBox="0 0 512 512"
```

or:

```text
viewBox="0 0 1024 1024"
```

Do not hardcode unnecessary raster-like details.

---

# 14. Icon Margin

Keep approximately:

```text
10–15%
```

of the canvas empty around the primary symbol.

The object should feel centered but does not need to be mathematically centered.

Optical centering takes priority.

---

# 15. Lighting

Assume a consistent conceptual light source:

```text
upper-left / upper-front
```

Therefore:

```text
top face      = brightest saturated blue
main side     = deep indigo
inner side    = pale blue
```

Do not use realistic shadows.

Depth should come primarily from:

- face direction
- color
- geometry
- negative space

---

# 16. Shadows

Avoid conventional drop shadows.

Bad:

```text
blurred black shadow
large ambient shadow
glowing shadow
```

Preferred:

```text
none
```

or occasionally a very subtle geometric projection plane.

Planura should feel like a **CAD drawing transformed into geometry**, not a rendered 3D object.

---

# 17. Border / Outline Policy

Do not outline every 3D surface.

Surface boundaries should usually be implied by color differences.

Use strokes mainly for:

- plans
- paths
- guides
- construction geometry
- tool interaction indicators

This keeps the visual hierarchy clean.

---

# 18. Curves

Straight geometry is the default Planura language.

Curves may be used when semantically relevant.

Examples:

```text
Arc tool
Circle tool
Orbit
Rotate
Follow Me
curved surface
```

Curves should contrast with the otherwise architectural straight-line system.

---

# 19. Avoid

Do not use:

- generic 3D cubes
- house icons
- skyscraper silhouettes
- photorealistic rendering
- glassmorphism
- heavy shadows
- excessive gradients
- neon effects
- thick black outlines
- cartoon styling
- overly detailed blueprints
- random perspective
- arbitrary decoration

Avoid anything that makes Planura look like:

```text
real-estate software
construction management software
game engine
crypto/Web3 product
generic CAD clone
```

---

# 20. Desired Brand Impression

Planura should communicate:

```text
Architecture
Geometry
Space
Precision
Creation
Clarity
Freedom
Modern design
```

A user should feel:

> "I can draw something simple and immediately turn it into space."

---

# 21. Tool Icon Design Formula

When creating a new Planura SVG tool icon, use this process:

### Step 1 — Identify the geometric subject

Example:

```text
Push/Pull = planar face
```

### Step 2 — Identify the action

```text
extrusion
```

### Step 3 — Represent the action spatially

```text
flat face
↓
raised face
```

### Step 4 — Apply Planura perspective

```text
isometric / architectural
```

### Step 5 — Apply Planura surface colors

```text
top       #4050F0
side      #2631A8
secondary #9EAFE9
```

### Step 6 — Simplify

Remove anything that does not improve recognition.

---

# 22. Example Prompt for SVG Generation

When generating Planura SVG icons, use instructions similar to:

> Create a minimal SVG icon for the Planura 3D modeling application.
>
> Use Planura's architectural visual language: clean geometric planes,
> isometric perspective, strong negative space, and a transformation from
> 2D geometry into 3D space where appropriate.
>
> Use deep indigo (#2631A8 / #3039C9) for primary structural faces,
> bright indigo (#4050F0) for top-facing surfaces,
> pale spatial blue (#D0D8FA / #9EAFE9) for secondary faces,
> and blueprint blue (#293AB4) for plan outlines and technical strokes.
>
> Use only a few large geometric shapes.
> Avoid generic cubes, heavy shadows, decorative effects, excessive detail,
> and thick black outlines.
>
> The icon must remain recognizable at small toolbar sizes.
> Prefer polygon/polyline geometry and clean SVG paths.
> Use a 512×512 viewBox with approximately 10–15% visual margin.

---

# 23. Core Design Principle

The most important Planura design rule is:

> **Draw the operation as geometry.**

Do not decorate an icon to explain its meaning.

Let the geometry itself explain the tool.

And whenever possible, preserve the central Planura metaphor:

> **A line becomes a plane.  
> A plane becomes a space.**
