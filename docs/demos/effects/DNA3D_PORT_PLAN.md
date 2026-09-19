# Port de `dna3d` (demoscene-repo-orig) al engine C++23

Estado: **planificado** (motor listo; port de efecto pendiente). Este documento inventaría el
original y fija cómo encajarlo con el sistema de plantillas/`Angle` antes de escribir la demo.

Original: `demoscene-repo-orig/effects/dna3d/dna3d.c` (554 líneas) + `effects/dna3d/data/`.

## 1. Qué hace el efecto

Un **ADN de doble hélice** que rota; los vértices se dibujan como **flares** (BOBs OR) y se
unen con **links** (líneas EOR). El fondo es una **foto `necrocoq`** con **color por línea**
(Copper) en un **doble playfield** (PF1 = flares/links, PF2 = foto).

- Geometría: `GenCircularDoubleHelix()` **regenera** los 80 vértices cada frame con
  `SIN`/`COS` sobre un `Node3D` (`POINTS_PER_TURN=10`, `TURNS=4`, radio 2.5 y hélice 0.5).
  El asset `dna.c` (`_dna_helix_data`, `obj2c`) sólo aporta la malla/plantilla.
- Animación: `phi_offset` avanza la fase de la hélice; `objectToWorld` la rota.
- Render: `TransformVertices` (proyección propia con `div16`), `DrawFlares` (bob indexado
  por `z`, `bltcon0 = A_OR_B`), `DrawLinks` (líneas EOR con patrón `0xffff`).

## 2. Assets

| Asset | Formato | Tamaño | Uso |
|---|---|---|---|
| `data/dna.c` | `obj2c` (`short[]` + `Mesh3D`) | 5 KB | malla plantilla de la hélice |
| `data/bobs.c` + `data/bobs_gradient.c` + `data/necrocoq-00-pal.c` | bitmap/paleta | 37 + 7 + 9 KB | flare + paleta |
| `data/necrocoq-00-data.c` | foto 256×256 | 59 KB | PF2 (fondo) |
| `data/necrocoq-01..10.c` | fotos | 8,9 KB × 10 | frames del fondo |
| `data/necrocoq-00..10-pal` (inline) | color por línea | | Copper de PF2 |

## 3. Convención de ángulo (regla §4-quater)

El original mide el ángulo como **índice 0..4095** (`SIN(a)=sintab[a&0xfff]`,
`include/fx.h`). En el port: `sin(turns(idx))`/`cos(turns(idx))` (tipo `Turns`, tabla 4.12
exacta), y al entrar a `math3d::load_rotate` el ángulo va como `Angle` (aquí `turns`, ya que
el giro del objeto es el índice del original). No se usan `sin_q12`/`angle_to_radians` en
código nuevo.

## 4. Mapeo al engine

| Original | Engine |
|---|---|
| `Object3D` + `NewObject3D` | `eng::object3d::Object3D` + `new_object3d` (blob `Span<u8>`, validado) |
| `dna.c` (`obj2c`) | `Mesh3D{bytes, Span<s16> groups}` (como `pilka.c`) |
| `SIN`/`COS` de la hélice | `eng::retro::sin/`cos`(`turns`)` (`fixed_trig.hpp`) |
| `TransformVertices` (propia) | `eng::lib3d::transform_vertices` (`proj`) o la propia si difiere |
| `DrawFlares` | `eng::amiga::OrBlobBatch` (lote de BOBs OR intercalados) |
| `DrawLinks` | `blitter_line_eor` del `MinimalBackend` |
| Doble playfield + color/línea | `copper::Plan`/`Scheduler` + `wait_position_pal` |
| Doble buffer | `MultiBuffered<...>` / 2 bitmaps (como 117) |

## 5. Pasos

1. Portar `data/dna.c` a `Mesh3D` con `Span` de bytes y grupos (longitudes medidas), como se
   hizo con `pilka.c`.
2. Portar `GenCircularDoubleHelix` con `Turns`/`q12` y `mul`/`>>16` fieles a los `MULVERTEX`.
3. Scaffold de la demo (`demos/amiga/NNN_dna3d`) reutilizando 117 (bobs/copper/doble buffer).
4. Flares vía `OrBlobBatch`; links vía líneas EOR.
5. Doble playfield con el fondo `necrocoq` y color por línea (Copper).
6. Test host del generador/malla + validación visual y codegen (gate §4-ter).

## 6. Decisiones abiertas

- **Fondo**: portar los 11 frames `necrocoq` (59 KB + 10×9 KB) o dejar un degradado y marcar
  el fondo como parcial.
- **`TransformVertices` propia vs `lib3d::transform_vertices`**: la del original usa
  `div16`/`MULVERTEX`; la del engine (`projector` + `div_wide`) da otra proyección. Mantener
  la propia en la demo (fiel) o unificar (más limpio).
- **`phi_offset`**: ¿avanza por frame como el original o `turns(frame*k)`?
