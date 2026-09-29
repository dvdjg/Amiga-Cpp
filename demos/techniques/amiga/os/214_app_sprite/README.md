# Demo 214 — sprite de juego por `App`/`Screen`

Gate del camino de **alto nivel** de dibujo de objetos (`PUBLIC_GAME_API.md` §2.1.1): la demo
no arma `BlitJob`s ni `BobTarget`. Describe su sprite con `eng::graphics::Sprite` y lo pinta
con **`app.screen().sprite(spr, x, y)`**; la geometría del destino la prepara la escena
(`Scene::bob_target()` → `DrawTarget`) y **`app.present()`** ejecuta el plan de Blitter.

## Qué muestra

- Escena planar estándar 320×256×4 con paleta base.
- Un **BOB 32×32 de 2 planos** (disco rojo con centro amarillo) dibujado por cookie-cut, con
  la hoja generada en Chip al arrancar.
- El sprite recorre la pantalla en horizontal y rebota en vertical.
- Todo por la fachada `eng::App`/`Screen`: `init(app)`/`update(app)`/`render(app)`.

## Criterio de aceptación

- `state=3` (Ready).
- Captura: disco rojo con centro amarillo sobre fondo azul oscuro.

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/os/214_app_sprite --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/os/214_app_sprite --warp --screenshot out/tmp/214.png
```

## Referencias

- API de juego: `docs/engine/architecture/PUBLIC_GAME_API.md` §2.1.1 y §5 (paso 6).
- `Sprite`: `engine/include/eng/graphics/sprite_asset.hpp` (gate host HOST-324).
- Escena/objetivo de dibujo: `docs/engine/architecture/SCENE_COMPOSITION.md`.
