# 079 - wireframe: README

Demo del **trazado de alambre por Blitter** con el objeto `pilka` (malla `obj2c` tipo balón de fútbol), porte del original `demoscene-repo-orig/effects/wireframe/wireframe.c` y tutorial del primitivo `eng::retro::wire_lines`.

## Efecto

El balón gira sobre el centro de una ventana 256×256×4 (registros del original: `SetupPlayfield(MODE_LORES, 4, X(32), Y(0), …)`), dibujado como líneas azules con rastro cromático: un anillo de 5 planos con la copperlist rotando los punteros por frame (el plano recién dibujado es el bit 3; los 3 anteriores pintan el rastro con los colores 8/4/2/1, como el parcheo de `BPLxPT` del original).

En la parte superior de la ventana muestra la franja de depuración (nombre + versión + contador de frames + fps del último segundo) en una **zona aparte de 1 bpp** activa todo el frame.

## Técnica

```text
  por frame (update)
    ├─ transform: visibilidad de caras (signo de normal·(cam−p0)) + marcas de aristas
    │             + transform/proyeccion 4.12 (`eng::lib3d::transform_vertices`)
    ├─ alambre:   `eng::retro::wire_lines` sobre el plano activo
    │             (preludio `blitter_lines_begin` + `blitter_line_prepare`/`_draw` por arista)
    ├─ copper:    instala la lista del anillo (swap sin tearing)
    └─ pre-clear: lanza el clear del plano OCULTO siguiente (sin esperar; se solapa)
```

- **Sincronización**: `Engine::run_frames` (latido por IRQ de VBlank + espera bloqueada en `Wait()`); el demo no usa la cola de fondo, así que el servicio de Blitter va desactivado (`set_blit_service_enabled(false)`): las esperas entre líneas son sondeo apretado sin llamada de servicio por vuelta.
- **Pre-clear solapado**: el plano que se dibujará en el próximo update está **oculto** en la copperlist vigente (los 4 mostrados son `active`, `active+2..active+4`), así que su borrado (blit lanzado sin esperar) corre en paralelo con la CPU del frame y el primer trazo del update siguiente lo sincroniza.
- **Franja de depuración (1 bpp, coste por frame CERO)**: el texto se pinta por palabras (`Surface::draw_text_words`) en su propio **plano completo de 1 bpp**; la parte estática una vez en `init` y los campos dinámicos (**contador de frames + fps del último segundo** con un decimal, `eng::debug::HudFpsWindow`) ~1 vez/segundo (~56 escrituras). El 5º plano va **activo todo el frame** en el header de la copperlist (sin WAITs: una zona `>255` no se puede cerrar con el comparador `>=` de WinUAE, `custom.cpp:4416` `coppercomp`) y la paleta 16..31 es un **espejo** de la del alambre (16 = tinta) para que el +16 del plano sea transparente. La franja queda en la banda superior (filas de ventana ≈16..23).

## Rendimiento

`A500_debug`, `measure-fps` (2026-10-17): **32,7 fps / 1,53 campos/frame**; con el build **release** (`-O2`): **37,04 fps / 1,35 campos/frame** (incluye el +0,11 campos del 5º plano del HUD; el HUD de campos dinámicos no cuesta por frame). El campo de fps del propio HUD coincide con `measure-fps` (validado con capturas separadas y su delta de contador). Antes de la modernización: 6,86 fps / 7,29 campos (el camino antiguo pagaba el O(2) del informe de presupuesto del `FramePlan` por línea, `memset`/`memcpy` por job del plan, sondeo de VBlank y servicio de blit por vuelta). Detalle por secciones y evidencia: `VALIDATION.md`.

## Uso

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/079_wireframe --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/079_wireframe
node tools/debug/measure-fps.mjs 079_wireframe A500_debug
```

`-DWIREFRAME_FREEZE_ANGLE=N` congela el ángulo en `N` vueltas (capturas deterministas para comparar builds con `tools/analyze/freeze-diff.mjs`).

## Reutilización (búsqueda previa al port)

- **Trazado de líneas**: no había camino reutilizable para líneas "copia" por Blitter con preludio único; se añadió `eng::retro::wire_lines` (`engine/include/eng/retro/wireframe.hpp`), reutilizando `blitter_line_prepare`/`blitter_line_draw` del backend y `OutlineEdge` de `flat_shade_xor.hpp`. Buscado y descartado duplicar: `Surface::draw_line` (ruta de plan, cara), `flat_shade_xor` (solo EOR/ONEDOT de contorno), `mesh_render_wire` (CPU vía `Surface`).
- **Modelo/matemática 3D**: `eng::object3d` (HOST-014) + `eng::lib3d` (HOST-047) — no se duplica el recorrido del object model.
- **Línea EOR/ONEDOT**: `flat_shade_xor.hpp` (demo 116), no aplicable (el original de 079 traza horizontales y sin ONEDOT).
