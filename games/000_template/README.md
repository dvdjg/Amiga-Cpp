# 000 — Plantilla de juego (Amiga)

Juego 2D mínimo que usa **solo la fachada** (`eng/api/api.hpp`): el juego describe *qué* quiere
(dibujar, mover, animar) y el engine decide *cómo*. No hay registros, `FramePlan`, `SceneResources`,
`compose`, memoria ni copperlist en el código de juego.

```bash
bash ./tools/build/build-demo.sh games/000_template --debug
bash ./tools/run/run-demo.sh games/000_template
```

## El patrón

`main()` es el **composition root** (una vez): elige el perfil de memoria (`configure_game_memory`),
declara el `GameDisplay` (geometría + paleta + efectos) y arranca (`set_display` + `start`). El
**juego** solo implementa `init`/`update`/`render` sobre la fachada.

| Concepto | API |
|---|---|
| Memoria (pool, tamaños) | `backend.configure_game_memory()` — automático por hardware |
| Display (planos, paleta, gradiente) | `GameDisplay` + `app.start()` |
| Dibujo | `app.screen()`: `clear_box`, `fill`, `frame`, `line`, `sprite`, `bitmap`, `c2p` |
| Objetos / animación | `eng::graphics::Sprite`, `eng::graphics::Anim` |
| Colisión | `eng::Box::overlaps` / `intersection` |
| Entrada | `app.input()` (`pad0/pad1/mouse/keys`) |
| Audio | `app.audio().play_music(name)` / `play_sfx(name)` |
| Mensajes (mini-SO) | `app.port()` |

## Coste y límites

- Bucle de **coste cero** con las primitivas del **plan del Blitter**: `clear_box` (fondo) y el
  objeto por `frame` (líneas). Medido: **~28 fps (1,75 campos)** con `clear_box` + `frame`; solo
  `clear_box` va a 50.
- **No mezclar inmediato con diferido**: `fill`/`clear` usan el rasterizador (CPU o Blitter
  inmediato) y se vuelcan **ya**, mientras `clear_box`/`sprite`/`text`/`frame` se **encolan** y se
  ejecutan en `present`. Dibujar un `fill` antes de un `clear_box` diferido hace que el `clear`
  lo borre.
- **Gaps de la fachada** (roadmap §4–§9):
  - **`screen().text`** usa el rasterizador de fuente por **CPU** (~24 k ciclos/glifo → ~2 campos
    por línea); la ruta por Blitter existe (`draw_text_blit`) pero no está cableada a la fachada.
  - No hay un **relleno de color diferido** (rectángulo sólido por Blitter en el plan); `fill` es
    inmediato y `frame`/`line` usan el modo línea (más lento).
  - La geometría de assets (el `desc` del `Sprite`) sigue siendo dato del juego y los blobs se
    registran con `INCBIN` (falta el pipeline/manifiesto).
  - Sin escenas/estados (§6), cámara/tilemap de juego (§7) ni unificación de vocabulario (§8).

