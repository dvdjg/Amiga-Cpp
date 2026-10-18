# `adv-stage/1` — formato de sala nativo Amiga (A500)

Extensión **propia del pipeline** para describir una sala de aventura **con información de hardware**: display y modos (single/DPF/**EHB**/**HAM**), capas de scroll por hardware, parallax, transparencias por truco de color, **Copper Score** (paletas y registros por línea con disparadores), canales de sprite, BOBs, HUD, streaming y **presupuesto declarado**. Es el documento que el compositor (A3 de [`ROADMAP_ADVENTURE.md`](../../../docs/guides/roadmap/ROADMAP_ADVENTURE.md)) baja a los tipos del engine (`scene::LayerPlan`, `scene::ScrollSpec`, `copper::Plan`, `eng::hud`); el diseño de capas está en [`ADVENTURE_LAYER.md`](../../../docs/engine/architecture/ADVENTURE_LAYER.md).

El SIM describe **qué hay** (geometría, paleta, objetos, guiones); el stage describe **cómo se presenta en el Amiga** y qué efectos demoscene se disparan. El andamio se genera desde el SIM (`formats/scaffold-stages.mjs`) y se edita en `edited/stages/` — es la fuente humana del port.

## Estructura

```text
adv-stage/1 {
  format, room, name
  display   { mode: single|dpf|ehb|ham, planes: 1..6, height: 1..256 }
  palette   { slots: 2..32, protectedSlots[], managedByCopper: bool, samples?: [#RGB…] }
  layers[]  { id, kind, band{top,height}, content{type:png|tiles|uaf, ref}, 
              scroll{kind: none|fine|copperRing|blitterColumns|copperSplit|strip,
                     mapPeriodWords, speedPx}, parallax (8.8, 256=1.0),
              transparency{kind:none|colorKey|dpfPriority, color}, field: 0|1|2 }
  copper    { budgetWordsPerLine: 0..64, cues[] }
  sprites   { maxChannels: 1..8, channels[{channel, role}] }
  bobs      { max, budgetWordsPerFrame }
  hud       { region{top,height}, elements[] }
  stream    { resident[], roomPack[], optional[] }
  budget    { chipBytes, planes, copperWordsPerLine, blitterWordsPerFrame, spriteChannels }
  notes[]   // texto libre del andamio
}
```

## Capas y su materialización en el engine

| `kind` | Se compone como | Uso típico |
|---|---|---|
| `pfNear` / `pfFar` | Campos de un DPF (`XlimitedScene`/`XlimitedDualComposer`) o single | fondo, parallax |
| `sprLayer` | Capa de sprites HW (`api/effects/sprite_layer.hpp`) | fachadas free-form, mar |
| `bobLayer` | `CompositeScene`/BOBs (degradado sprite→BOB) | actores, props |
| `panel` | Banda con modo/paleta propios (`WorldRegion` por banda) | retrato EHB/HAM |
| `fx` | Copper por línea (`copper::Scheduler`/`eng::Copper`) | agua, raster bars, destellos |
| `hud` | Región `eng::hud` (`Band`/`Overlay`) | barra de verbos, inventario, subtítulos |

`scroll.kind` mapea a `scene::ScrollKind`: `fine`→`Fine`, `copperRing`→`CopperRing`, `blitterColumns`→`BlitterColumns`, `copperSplit`→`CopperSplit`, `strip`→`Strip`. `parallax` es un factor **8.8** (256 = 1.0; 128 = mitad de velocidad). `field` fija el playfield preferido (`scene::LayerPlayfield`).

**Trucos de color** (`transparency`): `colorKey` = cookie-cut por índice transparente (BOBs/sprites), `dpfPriority` = transparencia del PF delantero por color 0, `none`. La prioridad sprite/playfield por banda se expresa con un `reg` del Copper Score (`BPLCON2`).

## Copper Score (partitura)

Cada `cue` es una lista de `tracks` y un disparador:

```text
cues[] { id, trigger{kind: onEnter|onScrollX|onActorY|onDialogue|onBeat|onScript, value}, frames, restore, loop, tracks[] }
tracks PaletteTrack { first, count, keyframes[{frame, colors[count]}] }   // color #RGB (4 bits/canal)
       RegTrack     { line, reg ("BPLCON1", "BPLCON2", …), value }
       WaitMarker   { line }  // ancla para parchear un tramo ya emitido
```

`keyframes` se **precalculan** (pasos en ROM/cocina) y en runtime solo se indexan; un cambio lento emite pocos MOVEs por frame. El coste se valida contra `copper.budgetWordsPerLine` (política A500, por defecto 16–32) — el validador estructural comprueba el rango; el compositor (A3) y `eng::setup::check_copper_list` cierran la coherencia real.

## Presupuesto y streaming

- `budget` declara el tope por sala (Chip, planos, palabras de Copper por línea, Blitter por frame, canales de sprite); el informe host (I4) lo contrasta con `eng::hw::bus_budget`.
- `sprites.maxChannels` ≤ 8 (A500); `channels[]` asigna roles (cursor, retrato, props) y no admite duplicados.
- `stream` es el manifiesto de la sala: `resident` (siempre en RAM), `roomPack` (lo que carga la sala) y `optional` (close-ups, música larga). El pipeline de carga es `AssetCache`/`eng::res` (ver `WORKSPACE.md`).

## Reglas de validación (implementadas en `stage.mjs`)

- `single`/`dpf`: `planes ≤ 6` y suma de campos DPF ≤ 6; `ehb`/`ham`: exactamente 6 planos.
- Bandas `top + height ≤ 256` (PAL sin overscan gestionado) y `display.height ≤ 256`.
- `palette.protectedSlots` dentro de `slots` (slots reservados al HUD/UI); `slots ≤ 32`.
- Rango de paleta de un `PaletteTrack`: `first + count ≤ 32`; cada keyframe aporta exactamente `count` colores.
- Canales de sprite únicos y `< maxChannels`; `maxChannels ≤ 8`.
- `parallax ≤ 1024` (8.8), `speedPx ≤ 32`; `copper.budgetWordsPerLine ≤ 64`.

Las cotas son **política de diseño A500 del pipeline** (documentadas aquí); las absolutas del chipset se validan en el compositor/validadores de setup.

## Flujo

1. `node tools/adventure/formats/scaffold-stages.mjs <juego>` → andamios en `edited/stages/` (no pisa ediciones).
2. Edición (humano/editor): capas, parallax, EHB/HAM, cues, sprites, presupuesto.
3. `validateStage()` (tests/CLI) + informe de presupuesto (I4).
4. El compositor (A3) consume el stage → plan del frame.
