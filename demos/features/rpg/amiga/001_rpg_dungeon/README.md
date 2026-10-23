# 001_rpg_dungeon — capa RPG del engine (fachada de juego)

Juego de rol mínimo por casillas construido **solo con la fachada** (`eng/api/api.hpp`, `App`/`Screen`) y la capa `eng::rpg`: recoges mineral, forjas una espada en el yunque (crafteo + mejora + afijo + equipo → ATAQUE 5→14), abres el cofre (derrama llave y pócima), comes la pócima (necesidades de `eng::sim`) y abres la puerta con la llave. El HUD de juego muestra inventario, ataque derivado, hambre y objetivo por palabras (`Font8`) en la banda inferior; nombre + versión + frame + fps los pone `HostHud` en el overlay del host.

## Cómo se ejecuta

```bash bash ./tools/build/build-demo.sh demos/features/rpg/amiga/001_rpg_dungeon --debug bash ./tools/run/run-demo.sh demos/features/rpg/amiga/001_rpg_dungeon
```

Controles: joystick/pad (direcciones = paso de casilla; fuego = verbo del contexto: forjar, abrir, comer). **Sin input se reproduce un guion determinista** (`kScript`) que completa la historia y después patrulla; sirve para validar movimiento y secuencias con capturas.

## Técnica (y por qué)

- **Sala pintada una vez**: `fill_box` (plan del frame) en los dos primeros frames (uno por buffer del doble buffer). Repintarla cada frame costaría ~hora y media de presupuesto: cada job del plan sincroniza con el Blitter (~15k ciclos medidos por job, DT-025).
- **Sprites por parches 16×16 en ROM**: bloques planares generados en compilación (`make_patch`) y copiados en Chip al arrancar; por frame solo se borra el rastro (`copy_now` del parche negro del tamaño de la caja envolvente a 2 frames) y se pinta el estado actual: un job por sprite en la racha de streaming. Medido: la escena entera (sala + parches) sostiene 50 fps con la CPU holgada.
- **HUD de juego por tira Chip**: dos líneas (inventario/ataque/hambre y objetivo) compuestas por CPU con los glifos de `Font8` en una tira planar de 320×24 y publicadas con **un blit por plano** por frame. Se recompone **solo cuando el estado mostrado cambia** (bandera sucia en los sitios de mutación): la composición per-frame fue el único trabajo realmente caro de la escena (DT-025). La identidad/frame/fps va en el overlay del host.
- **Movimiento por casillas de 16 px**: todo queda alineado a palabra para DMA (sin máscaras ni shifts en las copias) y da el aire clásico de CRPG.

## Búsqueda previa

Antes de dibujar objetos a mano se revisó el engine (regla §1.7): el camino de objetos retenidos con actores es `ActorStore` / `actor_emit` (`eng/scene/actor.hpp`, ver `docs/engine/architecture/OBJECT_SYSTEM.md`), pero esta demo **no usa los patrones de bajo nivel vigilados** por `prior-art` (no incluye `planar_band.hpp`, `blob_batch.hpp`, `add_world_bitmap` ni `add_world_window`): sus sprites son parches propios copiados por la racha de streaming de la fachada (`Screen::copy_now`), pensados para enseñar el coste real de OCS y el camino de la capa RPG. La migración al sistema de actores (si el vertical slice lo pide) es una decisión abierta del roadmap M6.

## Documentación de la demo

- `src/INTENT.md` — qué hace y qué demuestra, elementos y evolución temporal.
- `src/VISION.md` — evaluación con el modelo de visión local (prompt canónico).
- `VALIDATION.md` — medidas (fps/ciclos), evaluación de visión y límites declarados.

