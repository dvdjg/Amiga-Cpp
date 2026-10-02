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

- El bucle es de **coste cero** con las primitivas del Blitter (`clear_box`, `fill`, `sprite`):
  la plantilla corre a **~50 fps** (1 campo) con el bucle mínimo.
- **`screen().text` usa el rasterizador de fuente por CPU** (lento ~2 campos con una línea corta):
  para HUD conviene un font por Blitter (pendiente) o dibujar el texto una vez sobre un fondo.
- Faltan en la fachada (roadmap §5–§9 de `ROADMAP_GAME_API.md`): geometría de assets incrustada
  (hoy el `desc` del sprite es dato del juego), escenas/estados, cámara/tilemap de juego y
  unificación de vocabulario.
