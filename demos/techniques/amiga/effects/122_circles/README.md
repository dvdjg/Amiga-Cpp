# 122 — circles (port de `demoscene-repo-orig/effects/circles`)

Círculos concéntricos de 1 bpp dibujados por la **fachada de juego** (`eng::App` + `eng::Screen`). Es el port de `demoscene-repo-orig/effects/circles/circles.c`, elegido como caso de prueba del API: el original son **46 líneas** con tres includes (`effect.h`, `copper.h`, `circle.h`), un bitmap global y manipulaciones directas de bitplane; el port hace la misma figura sin nombrar hardware (ver «Comparación conceptual»).

## Efecto

```text
  original (46 líneas, libgfx C)                 port (fachada del engine)
  ──────────────────────────────                 ──────────────────────────
  screen = NewBitmap(320,256,1,BM_CLEAR)         GameDisplay { 320x256x1, paleta }
  SetupPlayfield(MODE_LORES,1,…)                 app.set_display(display) + app.start()
  SetColor(0,0x000); SetColor(1,0xfff)           display.palette = {0x000,0xfff}
  cp = NewCopList(100); CopSetupBitplanes(…)     (lo compone/posee el App)
  EnableDMA(DMAF_RASTER)                         (lo compone/posee el App)
  for (r=2; r<128; r+=2)                         for (r=2; r<=kMaxRadius; r+=2)
      Circle(screen, 0, 160, 128, r);                screen.circle(160, 124, r, 1);
  TaskWaitVBlank()                               bucle IRQ del motor (`App::run`)
```

El bucle de dibujo del efecto son **4 líneas** de API (`clear` + `circle` por radio); con la declaración del display y el `start()` se queda en ~12 líneas. El `Circle` original recibe `(bitmap, índice de plano, x, y, r)` y escribe el bit del plano 0; `Screen::circle` recibe `(x, y, r, color)` y el contexto decide planos, mapeo y recorte. La diferencia de forma es la del objetivo: el plano desaparece; el color de paleta entra.

## Comparación conceptual

| Pregunta | `circles.c` (libgfx) | 122 con el engine |
|---|---|---|
| ¿Quién posee el bitmap? | `NewBitmap` (heap de Exec) y `DeleteBitmap` a mano | `GameDisplay` + `Scene`; el `App` reserva el bloque Chip y lo libera en el teardown |
| ¿Quién elige el modo/planos? | `SetupPlayfield(MODE_LORES, DEPTH, …)` + `CopSetupBitplanes` | el planner de `App::start()` (`composition::compose`) a partir de `GameDisplay.color_depth` |
| ¿Quién arma la copperlist? | `NewCopList`/`CopSetupBitplanes`/`CopListActivate` | el `App` (copperlist tipada, doble buffer, `takeover`) |
| ¿Cómo se dibuja un círculo? | `Circle(screen, 0, x, y, r)`: bitmap + **índice de plano** crudo | `screen.circle(x, y, r, color)`: color de paleta; sin plano ni puntero |
| ¿Quién sincroniza el frame? | `TaskWaitVBlank()` (sondeo/sleep de exec) | bucle del motor por IRQ de VBlank (`App::run`, mini-SO) |
| ¿Quién valida que el dibujo cabe? | nadie: `Plot` escribe en `bitmap->planes[plane]` sin límites | `Surface` (clip) + `Playfield::draw_circle` (mapeo y límites del bitmap) |
| ¿Dónde vive la figura? | en la demo (`circle.h`/`Circle.c` de libgfx) | primitiva de dominio reutilizable (`Playfield::draw_circle` + `Surface::circle` + `Screen::circle`) |
| Versión/HUD en pantalla | no | franja inferior 1 bpp (`Screen::text_words`), norma del repo |

Lo que el port **gana**: misma figura en un contexto de juego (recursos con dueño, composición de display, teardown, doble buffer disponible, HUD y telemetría) y una primitiva reutilizable por cualquier juego (cualquier número de planos/color, cualquier resolución, CPU o Blitter). Lo que **cuesta**: el bootstrap (backend + memoria + display + run-status) son ~40 líneas que el original no tiene; el efecto concreto sigue siendo la parte trivial. Reducir ese bootstrap a un `Demo` base es el siguiente paso del roadmap («Próximos pasos»).

## Contrato y seguridad de buffers

`Screen::circle` es el camino de dominio: `Screen` (clip) → `Surface::circle` (caja envolvente y clip) → `Playfield::draw_circle` (mapeo lógico→físico, cualquier nº de planos/color, espejo). Nadie ve planos, `BPLxPT` ni punteros.

Los recursos que sí tocan DMA están acotados en el tipo y validados en la emisión:

- **Copperlist**: `Block<CopperTag, Chip>` + `copper::Scheduler`/`ListBuilder` con **capacidad** — `move`/`write_pair` fallan (y marcan `ok()==false`) si el par no cabe, incluso con una reserva degenerada (`< 2` palabras), en vez de escribir fuera.
- **Bitmap/`BobTarget`**: `bob_detail::target_valid` exige que la **reserva cubra la geometría** (`storage_bytes_used`, con `plane_step` explícito) y `box_fits` rechaza cajas que el Blitter escribiría fuera de la fila/alto del destino (el Blitter no recorta). Aplica a `bob_draw`, `bob_erase_box`, `bob_save_box`/`bob_restore_box` y `scene::clear_box`. Cubierto por **HOST-439** (casos fuera de buffer y reserva corta).
- **`FramePlan`**: `begin_blit_job`/`begin_line_job` devuelven una ranura de **descarte** con el plan lleno (nunca `m_blit_jobs[max]`, que pisaba el siguiente miembro) y el `commit` falla. Cubierto por **HOST-388**.
- **Racha de blits (`Screen::stamp`/`clear_now`)**: rechaza sprites/rects que no caben en el destino y exige `x` alineado a 16 en `clear_now` (el blit D-only de la racha no lleva máscaras de borde).

El inventario de huecos y el plan de cierre están en [`ROADMAP_API_COHERENCE.md`](../../../../docs/guides/roadmap/ROADMAP_API_COHERENCE.md) §F8.

## Fidelidad con el original

- Misma estructura (círculos de radio par, centro de pantalla, blanco sobre negro) y mismo algoritmo de punto medio/Bresenham entero.
- Centro en (160,124) y radio máximo 122 en vez de (160,128)/126: la franja inferior de 8 filas es del HUD (norma del repo). El resto de radios 2..122 coincide con el original.
- El original redibuja la figura completa cada VBlank (visualmente estática); el port la construye progresivamente (un radio por frame) y luego no la toca: el estado estable es idéntico y el coste por frame vuelve a cero. La construcción progresiva evita meter ~150 campos de coste en un solo frame de debug (medido: con el dibujo completo en 2 frames, la ventana de `measure-fps` promediaba 28 fps por el arranque; con un radio por frame, 49,87 fps).
- El HUD no existe en el original (norma del repo desde 2026-10).

## Búsqueda previa

Antes de añadir nada se buscó una primitiva de dibujo de círculo/píxel en `engine/include` (`grep -rn "circle\|set_pixel\|draw_span"`): existían `Surface::set_pixel`, `Surface::fill_rect`/`draw_line`/`fill_polygon`, `DrawTarget` y `Screen` (primitivas de dominio), pero **ninguna figura circular**. No se reimplementó nada en la demo: la primitiva se añadió al engine (`Playfield::draw_circle` + `Surface::circle` + `DrawTarget::circle` + `Screen::circle`) y se reutiliza desde la fachada. No aplican las reglas de `prior-art-rules.txt` (no se usan bandas planares, `blob_batch`, `add_world_bitmap` ni cadenas de sprites).

## Verificación

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/122_circles --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/122_circles
node tools/debug/measure-fps.mjs 122_circles A500_debug --json
```

Medida y validación visual en `VALIDATION.md`; intención en `src/INTENT.md`; evaluación del modelo de visión en `src/VISION.md`.
