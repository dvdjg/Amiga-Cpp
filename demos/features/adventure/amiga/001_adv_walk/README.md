# Demo 001 — Paseo por walkboxes con cámara de mundo (A0)

Demo de la capa de aventura (`eng::adv`) sobre la fachada: un mundo propio de 352×208 con **tres walkboxes convexas**, un actor que camina por la ruta calculada hasta el destino pedido (click o guion), **cámara con dead-zone y límites** (ventana de display por hardware con `BPLCON1`/punteros), hotspots con etiqueta en el HUD y HUD de depuración en su banda (1 plano, texto por palabras). Qué pretende demostrar y evolución temporal: `src/INTENT.md`; evaluación de visión (prompt canónico): `src/VISION.md`; medidas, alcance y bug abierto: `VALIDATION.md`.

```bash
bash ./tools/build/build-demo.sh demos/features/adventure/amiga/001_adv_walk --debug
bash ./tools/run/run-demo.sh demos/features/adventure/amiga/001_adv_walk
# Click de ratón inyectado (mueve el puntero y fija destino):
bash ./tools/run/run-demo.sh demos/features/adventure/amiga/001_adv_walk --mouse-click-at 60,40
```

## Técnica

- **Mundo y ventana**: bitmap de 368 px (mundo 352 + 16 de margen delantero) en 4 planos, filas de 46 B; fetch de 21 palabras (`DDFSTRT=$30..DDFSTOP=$d0` = 42 B) con `BPL1MOD/BPL2MOD = 46−42 = 4`. La ventana se mueve parcheando los `BPLxPT` (coarse) y `BPLCON1` (fino) con `fine_scroll_coarse`/`fine_delay` (`engine/include/eng/graphics/playfield_scroll.hpp`): display_start == cámara+16, continuo 15→0.
- **Fondo estático**: se pinta una vez por CPU en el bitmap (bandas, mar, arena, muelle, cartel, pared+puerta por función constexpr de color por píxel).
- **Actor y puntero**: BOBs enmascarados con **cookie-cut `$CA`** y máscara explícita (`BlitJobKind::MaskedBlobNoSave`, un job con `bitplane_count=4` + strides por plano, como la demo 050), arte en **Chip RAM** (copiado de las hojas constexpr; el Blitter no lee ROM) y **save-under** propio (CopyRect restore/stash al scratch). El arte es 16 px + 16 de guarda para el `source_shift` exacto.
- **HUD**: banda desde la línea 252 (Copper `wait_line`) con `BPLCON0` a 1 plano, `BPL1MOD/BPL2MOD=0`, `BPLCON1=0`, `DDFSTOP=$d8` (22 palabras = 44 B de fila, receta de la 080) y COLOR00/01 propios; texto por palabras (`playfield::Surface::draw_text_words`).
- **Cámara y navegación**: `eng::adv::Camera<Fixed<s16,4>>` (dead-zone ±60/±260, límites [0..32]) y `eng::adv::walkbox_route<s16>` (scratch de tres arrays estáticos, sin heap); el avance por la ruta es Bresenham a 2 px/frame sin multiplicaciones.

## Búsqueda previa

Patrón de bajo nivel ya abstraído y reutilizado en esta demo (candidato del engine verificado):

- **Ruta por walkboxes y cámara**: se usa `eng::adv::walkbox_route` y `eng::adv::Camera` (cabeceras `engine/include/eng/adv/navigation.hpp` y `camera.hpp`), no una reimplementación local.
- **Texto de HUD**: se usa `eng::playfield::Surface::draw_text_words` (`engine/include/eng/field/surface.hpp`), nunca píxel a píxel.
- **Ventana de scroll**: se usan `eng::graphics::fine_scroll_coarse`/`fine_delay` (`engine/include/eng/graphics/playfield_scroll.hpp`).
- **Jobs de Blitter**: `eng::graphics::BlitJob` + `AmigaBackend::blitter_submit` (no se programan registros del Blitter a mano).

## Estado

Funcional (ruta, cámara con paneo real, click, puntero, HUD y versión) y evaluado con visión (`src/VISION.md`, contornos limpios y sin artefactos). Arranque por `eng/platform/amiga/entry.hpp` (sin `SysBase`, `RunStatus` ni cabeceras de soporte propios; fallos con etapas con nombre) y copias a Chip por vista tipada (`view.as_words()`): gates `demo-ergonomics` y `demo-raw-writes` en verde para esta demo. **VERIFICADA**: ~48-49 fps estables (~1,0 campos/frame con `measure-fps`), flicker OK (`flicker-baseline.json`, `max_candidates: 0`) y visión canónica OK (`src/VISION.md`). La serie de cierres: espera de VBlank con sondeo activo (carrera de despertar, `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`), HUD por diferencias (`eng::playfield::DiffText`, engine; sobre la primitiva `Playfield::store_plane_words_row`), servicio de Blitter de fondo desactivado y clamp de la ventana de los bobs. Detalle en `VALIDATION.md`.
