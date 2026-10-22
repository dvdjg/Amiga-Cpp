# Consultation: flat-shaded 3D polyhedra on an A500 at 20+ fps

I need external advice on the rendering approach and its performance. Below is the complete, self-contained context: hardware, scene, exact algorithm and measured costs. Please answer in English, with concrete, Amiga-OCS-specific techniques and expected cycle counts.

## 1. Target and hardware

- Amiga 500, OCS (Agnus/Denise), PAL, 68000 at 7.09 MHz, 512 KiB Chip RAM, no Fast RAM.
- Display: 320×256, lowres, **5 bitplanes** (32 palette entries), 2 display buffers (double buffered by patching `BPLxPT`; copperlist built once). Rows 0..207 are the 3D viewport; rows 208..255 are a 1-bitplane HUD band.
- A PAL field is ~141,876 CPU cycles. Video bitplane DMA for 5 planes at 320 px takes 5×20=100 words/line out of ~227 bus slots/line.
- Goal: **20 fps or better** for the animation (the repo's floor for polygon-fill demos is 25 fps, so 20 is already a relaxation). Current result: **~4 fps**.

## 2. Scene

- Central solid: an **icosidodecahedron** (30 vertices, 60 edges, 32 faces: 20 triangles + 12 pentagons), radius 90 world units, rotating about its own center with three slowly drifting Euler angles (the rotation axes wobble slightly over time).
- A **cube** (half side 24, 8 vertices, 6 quads) orbits the central solid on an ellipse (world radii 210×150 at z=520±30) while rotating on its own, also with drifting axes.
- Camera: pinhole, focal 256, screen center (160,104), near 64; objects at z≈430..610, so they are always well inside the frustum and never cross near.
- The orbit was chosen so the **projected silhouettes never overlap** (verified numerically at start-up); this allows painter-order per solid without a z-buffer and without a silhouette clear pass.
- Faces: 30 distinct palette colours for the 32 polyhedron faces (two non-adjacent triangles share a colour), the cube reuses six of those hues on distinct faces. All adjacent faces have different colours.

## 3. Exact per-frame algorithm (current)

1. `scene.commit()` publishes the previous back buffer by patching the 5 `BPLxPT` pairs at the start of the field; drawing continues into the other buffer (no tearing).
2. Transform the 30 polyhedron vertices by an integer affine matrix (12-bit sine table, `Rx·Ry·Rz`, translation +z=520) and the 8 cube vertices likewise (+ orbit center).
3. Project each vertex **once per solid** with integer fixed-point: `sx = 160 + x*256/z`, `sy = 104 + y*256/z` (signed 16-bit math, `divs.w`).
4. Back-face culling per face using the 3D normal: `(b-a)×(c-a) · (0-a) > 0`, all products in s32 (`muls.w`). Pentagons use their first three vertices. No per-face clipping.
5. Screen-space bbox/area checks: drop degenerate faces and slivers <2 px.
6. Painter order: face array order (visible faces of a convex solid do not overlap, and the two solids do not overlap on screen).
7. Fill. Three variants were implemented and measured:
   - **A. Blitter, per plane, contour + area fill** (default): for each of the 5 planes, clear a dirty rectangle (union of the object bboxes in the current and previous two frames), then draw the edges of every visible face whose colour has that bit set with the Blitter in **line mode ONEDOT with minterm EOR**, then one **FILL_XOR area fill** over the faces' bounding box for that plane.
   - **B. CPU spans**: for each face, `convex_spans` (DDA per scanline) writes planar spans with `draw_span` for all 5 planes (opaque overwrite).
   - **C/D. Blitter mask + cookie-cut**: build a 1-bit mask per face (CPU spans) or per plane (union of faces with that bit) and copy it to the colour planes with `BLTCON` A→(set) / ¬A→(clear) cookie-cut blits.

The geometry path (1–6) is verified: at two frozen poses the rendered image matches an independent 2D reference (same table/projection/culling/painter) on 98.8 % of pixels, with differences only 1–2 px along polygon edges; no interior background pixels were found across 12 animated frames. So the slowness is purely fill cost.

## 4. Measured costs (emulator cycle counter, 7.09 MHz; 1 field = 141,876)

Profile sections (cycles/frame, 5 planes):

| Section | Cycles/frame | Notes |
|---|---|---|
| Frame (whole update) | 1.25–1.55 M | ≈ 8.8–10.9 fields |
| Projection + culling | ~148–155 k | 38 vertex projections + 38 face culls |
| Fill (A: per plane) | ~1.02–1.05 M | of which **line drawing ~891 k** |
| — clear dirty rect | ~40 k | 5 planes |
| — contour lines | ~891 k | ~160–190 ONEDOT EOR line Blitter ops/frame ⇒ **~5 k cycles per line** |
| — area fill | ~50 k | 5 planes, FILL_XOR |
| HUD | ~23 k | repaint every 8 frames |

Other variants: B (CPU spans) fill ≈ 2.35 M; C (mask+cookie per face) fill ≈ 1.04 M; D (mask per plane + cookie) ≈ 1.0 M. All land at 2.7–4.1 fps as reported by the on-screen fps counter (frame cadence 9–16 fields).

## 5. Facts about the fill path that puzzle me

- The Blitter **does** implement polygon fills (line mode + area fill), and the contour merge by parity works, but the cost is dominated by the **number of line Blitter operations**, not by pixels: ~170 lines/frame at ~5 k cycles each.
- Each line op reprograms `BLTCON0/1`, `BLTBMOD/AMOD`, `BLTAPT`, `BLTCPT`, `BLTDPT`, `BLTSIZE` (several custom-register writes at ~57 cycles each under `cpu_cycle_exact`) and waits for the previous line to finish (line mode mutates BLTDPT, so it must be rewritten per line).
- With 30 distinct face colours and 5 planes, an internal edge between two visible faces is drawn on every plane where the two colours' bits differ (≈2–3 planes per edge), which multiplies the ~70 visible edges into ~170 line ops.
- Polygon area fill itself is cheap (~50 k for all planes). The wall is the outline tracing.

## 6. Questions

1. **Is ~5 k cycles per ONEDOT/EOR line Blitter op plausible on OCS under 5-plane DMA, or is this a sign of a fixable inefficiency?** What is the theoretical cost of a 40–90 px line in line mode with per-line register reprogramming, and what could legitimately be hoisted or batched without corrupting the parity trick?
2. **What is the right architecture on an A500 for many flat-shaded, differently-coloured convex faces (30–40 visible faces, 320×208, 5 or 4 planes) at 20–25 fps?** Please compare concrete options and give expected costs: per-plane contour + XOR area fill (current); per-face 1-bit mask + cookie-cut (current variant C); CPU scanline fill into a chunky buffer + C2P (the engine has a 4bpp C2P); precomputed span/edge tables per face orientation or per frame; 4 planes with 16 solid colours plus 2×2 / 4×4 dither combinations for extra apparent colours; anything else that is OCS-appropriate (Blitter line-mode tricks, pre-shifted edge stamps, using the Copper to change colours per scanline or per object, etc.).
3. **How should dithering be combined with per-face colours efficiently?** I want to keep 4 bitplanes (16 palette entries) and use checkerboard/2×2 patterns between two entries to get more distinguishable face colours. With the Blitter's area fill this seems to require a patterned fill per face (two masks or a pattern applied per plane). What is the cheapest correct way to fill a convex polygon with a chosen 2-colour dither on 4 planes, and how much does it cost versus a solid colour?
4. **Should we reduce the number of distinct face colours instead?** For defect detection we wanted one colour per face; what colouring/budget do you recommend for 32 faces on 4–5 planes if we want 20 fps, and is a 3-colouring plus dithering a reasonable compromise?
5. **Are there known A500/demoscene references for fast flat-shaded polyhedra** (e.g. flatshade-convex, lib3d, 3D Construction Kit style) whose fill strategy we should copy? Please name the specific technique (registers, order, batching) rather than general advice.

## 7. Constraints

- 68000 only (no 68020 instructions, no FPU), no self-modifying code required, no extra RAM beyond Chip.
- The engine already has: copperlist patching, a Blitter line/area-fill routine (`fill_polygons_by_plane_patterns`), a per-face `blitter_fill_polygon` (mask spans + cookie-cut per plane), a CPU `convex_spans` filler, and a 4bpp C2P primitive.
- We prefer reusable engine changes over demo-specific assembly, but an engine asm primitive is acceptable if it is the right answer.
