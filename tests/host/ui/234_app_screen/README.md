# HOST-234: fachada de juego `eng::App` + `eng::Screen`

Test host del **borrador del API público de juego** (`eng/api/game.hpp`): `App` junta el bucle, la
pantalla y las tareas; el juego se escribe con `init/update/render(App&)` (con `auto&`, sin nombrar
el tipo) **sin ver** el backend, `GameContext`, `FramePlan` ni planos. `Screen` es el contexto de
dibujo de alto nivel (análogo al `RastPort`).

## Qué comprueba

1. `compose()` construye una escena planar y el juego la registra con `app.bind_scene()`.
2. `app.run(1)` llama a `init`, `update` y `render` del juego.
3. `app.frame()` = 0 en el primer frame.
4. `app.screen()` + `s.fill(Box, color)` dibujan; `app.present()` publica.
5. El `fill` de 4×4 color 1 deja 16 bits en el plano 0 y nada en el plano 1.
6. `scene.bind_hw_info(hw)` publica el display de la escena en el `HwInfo` (320×256×4, 16 colores).

El backend de prueba es mínimo (`boot` + `wait_vblank`), sin `execute_frame_plan`: `App::present`
lo omite con `if constexpr` (el juego normal no lo ve).

Objetivo y mapeo desde el API interno: `docs/engine/architecture/PUBLIC_GAME_API.md`; principios:
`PUBLIC_API.md` §1.1.

## Salida de referencia

```
OK: App/Screen (fachada de juego) validados.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/234_app_screen
```
