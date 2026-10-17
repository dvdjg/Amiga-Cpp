# Juego 400 — RPG SLICE (aldea + bosque + mazmorra)

Vertical slice jugable de la **capa RPG** del engine (`eng::rpg` sobre la fachada `eng/api`), en un A500 (OCS, PAL). Tres salas de 20×14 casillas de 16 px con una crónica completa: hablar con la anciana, cazar tres lobos, vender pieles, comprar la llave, abrir el torreón y salir con la reliquia (`OBJ:VICTORY`). Se juega solo (guion determinista) o con el pad.

- Intención y elementos en pantalla: [`src/INTENT.md`](src/INTENT.md).
- Evaluación de visión (prompt canónico): [`src/VISION.md`](src/VISION.md).
- Medidas y conclusiones: [`VALIDATION.md`](VALIDATION.md).

## Técnica

- **Sala por imagen cocinada, publicada solo al cambiar**: cada sala se cocina una vez en `init` a una imagen Chip plane-major de 320×224 (tiles desde una **tabla ROM** `constexpr` por fila/plano). Entrar en sala solo conmuta el bloque (`m_room_imgs[m_room]`) y programa la publicación en los dos buffers; cero coste por frame.
- **Actores por streaming**: jugador, lobos, botín y parches de puerta/cofre se copian con la racha `copy_now` (un blit por plano y objeto). Las estelas se borran **restaurando la casilla desde la imagen cocinada** (historial de 2 frames, patrón de la demo 001), sin repintar la sala.
- **HUD fusionado**: las dos líneas viven en las filas 208–223 de la imagen; al cambiar se republica solo esa banda (4 blits de 20×16 palabras).
- **Navegación del guion con el buscador del engine**: los destinos del guion se alcanzan con `eng::util::astar` sobre la rejilla de la sala (32×16 con relleno no transitable), rodeando NPCs, árboles y lobos en movimiento; la ruta se replanifica al cambiar de paso/sala o si el siguiente tramo se tapa.
- **Sincronización**: bucle de **sondeo** del engine (`App::run_polling` → `Engine::run_frames_polling`), sin depender de la IRQ de VBlank (bajo AmigaDOS vivo la ROM reescribe `INTENA` y puede apagar `VERTB`: `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`).

## Búsqueda previa

Antes de escribir el render se revisaron los patrones ya abstraídos por el engine para esta técnica: la copia de imágenes y parches por streaming existe en `eng::Screen::copy_now`/`stamp` (`engine/include/eng/api/screen.hpp`; usada por la demo `demos/features/rpg/amiga/001_rpg_dungeon`), el dibujo de objetos por intención en `eng::scene` (`eng/scene/actor.hpp`, `blob_batch.hpp`/`planar_band.hpp`) y el HUD ligero de la demo 120. Este juego no usa `scene::ActorStore`: es un slice de la capa RPG con parches 16×16 sobre una imagen cocinada (la lección de coste de DT-025), y reutiliza la fachada (`eng::Screen`, `eng::Block<Tag, Chip>`, `blit_ptr`).

## Flujo de trabajo

```bash
bash tools/build/build-demo.sh games/400_rpg_slice --debug
bash tools/run/run-demo.sh games/400_rpg_slice
node tools/debug/measure-fps.mjs 400_rpg_slice A500_debug --json
```
