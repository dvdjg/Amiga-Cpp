# Demo 121 - PROC CORNER LAYERS (autotiling por esquinas + mapa por capas)

Tutorial de las dos piezas nuevas de `eng::gfx::proc` sobre el display indexado de la 119
(`eng::IndexedDisplay<5,1>`, 320×256, 32 colores), con la escena generada **en
compilación** (`constexpr`, límites subidos en `build.args`):

- **Transiciones por esquinas (dual-grid)**: un `OccupancyMap` de 21×16 celdas (río de
  norte a sur con meandros + lago de contorno ruidoso con isla interior, `stamp_river`/
  `stamp_disc`) define el terreno; por celda de render se calcula la **máscara de 4
  esquinas** (`corner_mask`) y se elige UNA de las 16 variantes del set pintadas con
  materiales del atlas (`paint_corner_materials`: hierba/agua/espuma). Las orillas son
  orgánicas y **casan por construcción** (los vecinos comparten el valor de la esquina).
- **Mapa por capas**: `LayeredTileMap` (ground/decal/overhead) compuesto con
  `render_layered_map` directamente sobre el framebuffer de runtime (Span + stride, sin
  copia intermedia): flores en la capa decal y **copas de árboles con transparencia**
  (overhead, índice 0 = transparente, silueta de `SoftCircleMask` + material de follaje).
- **Actor entre capas (por debajo de las copas)**: un bicho de 16×16 (cuerpo blanco 31,
  color ÚNICO de la escena) camina por el carril y pasa bajo las copas del bosque. La
  escena se publica una vez (C2P); por frame solo se parchea la ventana planar del actor
  (`IndexedDisplay::capture_band`/`restore_band`/`patch_band` sobre `planar_band.hpp`):
  se restaura el fondo capturado y se pinta el actor con `máscara del actor & ~máscara de
  copas` — los píxeles de copa no se escriben y el bicho se oculta gradualmente al entrar
  y reaparece al salir.
- **HUD**: franja inferior con nombre + versión + contador de frames por **parcheo planar
  de texto** (`patch_text`, tinta 30 — el 31 se reserva al actor para el conteo objetivo
  de elementos).

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/121_proc_corner_layers --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/121_proc_corner_layers --warp
node tools/debug/measure-fps.mjs 121_proc_corner_layers A500_debug --json
```

Ablación de medición (scaffolding compilado, coste cero por defecto): `K121_SKIP_ACTOR`
desactiva el parcheo del actor (restaurar + repintar) para cuantificar su coste por frame
con `EXTRA_DEFINES='-DK121_SKIP_ACTOR=1'` (procedimiento A/B de
`docs/guides/optimization/METODO_OPTIMIZACION.md`).

## Estado (ver [`VALIDATION.md`](VALIDATION.md))

Generación, composición por capas y parcheo del actor en hardware; el estado de cierre
(F3/F4) queda en `VALIDATION.md` con las medidas y las respuestas crudas de visión.
