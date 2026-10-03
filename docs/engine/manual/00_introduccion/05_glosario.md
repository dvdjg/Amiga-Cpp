# Glosario

Vocabulario del engine. El resto del manual lo usa sin redefinirlo. Las definiciones canónicas de
diseño viven en `docs/engine/architecture/` (se citan).

## Juego y bucle

| Término | Significado |
|---|---|
| **`App`** | Composition root del juego (`eng/api/game.hpp`): bucle, memoria, entrada, audio, escena, presentación. |
| **`Game`** | Tu tipo con `init`/`update`/`render(App&)`; una **escena** (`push_scene`) puede sustituirlo. |
| **`GameContext`** | Estado por frame que el engine pasa a las capas internas (`FrameStats`, cola de fondo). |
| **`GameDisplay`** | Descripción del display (ancho, alto, profundidad, paleta, efectos). |
| **VBlank** | Latido de 50 Hz; marca el frame. El mini-SO lo publica. |

## Dibujo y pantalla

| Término | Significado |
|---|---|
| **`Screen`** | Contexto de dibujo de alto nivel: `clear`/`fill`/`frame`/`line`/`text`/`sprite`. |
| **`Scene`** | Escena retenida: bitplanes + copperlist + programa; la **posee** la fachada/el juego. |
| **`Sprite`** | Asset cocinado (geometría + bitmap + máscara) que dibuja `screen.sprite(...)`. |
| **`SpriteScene`** | Fachada de sprites de nivel A: `add(ActorDesc)` + `emit` (HW sprites + BOB fallback). |
| **`IndexedDisplay`** | Framebuffer **indexado** de nivel A: escribes índices, hace C2P + swap (efectos por píxel). |
| **Planar / Chunky** | Layout de vídeo del Amiga (1 bit/plano/píxel) vs. lineal (1 byte/píxel). El **C2P** convierte. |

## Scroll, tiles y escena

| Término | Significado |
|---|---|
| **`ScrollLayer`** | Contrato que el `App` arranca y conduce por frame (tiras, corkscrew…). |
| **`ScrollPlan`** | Vocabulario común: geometría + política + contenido + `Parallax`. |
| **`ScenePlan`** | Lista de capas (`LayerPlan`) que el planner resuelve en una estrategia. |
| **`RasterLayout`** | Composición de pantalla por **bandas** (DPF, split-screen, HUD). |
| **Corkscrew / XYLimited** | Scroll **8-way** (anillo + split de Copper); `ScrollKind::CopperSplit`. |
| **Tira / Strip** | Scroll columnar (un blit por frontera de tile); `StripScrollLayer`. |

## Memoria, tipos y recursos

| Término | Significado |
|---|---|
| **Chip / Slow / Fast** | Bancos: Chip (la ve el DMA), Slow (no-Chip, auxiliar), Fast (CPU-privada). |
| **`Tag`** | Marca de dominio de una vista/bloque (`PlaneTag`, `BobTag`, `CopperTag`, `ChunkyTag`…). |
| **`Block<Tag>`** | Bloque reservado en un banco, tipado por dominio (medio en `block.kind`). |
| **`Bytes<Tag>` / `Words<Tag>`** | Vista de bytes/palabras con tag de dominio (frontera `raw()` documentada). |
| **`MemoryManager`** | Reparte bloques por banco; `reserve<Tag>(n, align)`. |
| **`AssetCache`** | Caché de assets con estados, prioridad/refcount (R6). |
| **`Vfs`** | Fachada de ficheros: paths normalizados, mounts, `read_all`, enumeración, handles. |
| **`.engz` / HUNK / `.englib`** | Contenedor comprimido / formato nativo AmigaOS / librería dinámica propia. |

## Mini-SO, audio, IA

| Término | Significado |
|---|---|
| **`eng::os`** | Mini-SO: `MsgPort`/mensajes, timers, E/S asíncrona, VFS, tareas de fondo. |
| **`AudioSystem` / `AudioMixer`** | Música (Pt/P61) y mezcla de SFX (voces de Paula). |
| **`Sensor`/`Steering`/`GOAP`** | IA: percepción, movimiento, planificación. |
| **`board` / `cards`** | Motores de IA de tablero (ajedrez/Go) y de naipes. |

## Herramientas (fuera del runtime)

| Término | Significado |
|---|---|
| **`g_eng_run_status`** | Símbolo que publica `InitStarted`/`Ready`/`Failed` + `detail` (lo lee el runner). |
| **Canal lateral** | Puerto del emulador para leer memoria/estado sin parar la CPU (`debug-winuae-v2-guide.md`). |
| **`build-demo.sh` / `run-demo.sh`** | Compilar/ejecutar una app (ver `docs/build/BUILD_AND_RUN.md`). |

Volver al [índice de introducción](README.md) · [índice del manual](../README.md).
