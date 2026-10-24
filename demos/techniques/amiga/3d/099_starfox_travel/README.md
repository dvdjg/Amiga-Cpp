# 099 - Star Fox style rail travel

Traveling **no jugable** inspirado en los niveles de espacio de Star Fox (SNES): un caza teal del jugador, dos cazas enemigos en tonos salmon/granate que se acercan, hacen weave y se inclinan/cabecean, disparos de ambos bandos, estrellas en movimiento y un fondo espacial con banda superior, planeta, luna y rocas grises. La cámara avanza por un rail fijo; todavía no hay entrada, colisión, puntuación ni audio.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release
```

El número 099 se eligió para no ocupar el 097 que el plan de presentación 3D reserva para la demo de horizonte Copper.

## Técnica

Los modelos (caza y enemigo) se construyen en código como sólidos convexos de 5 vértices y 6 caras triangulares (`retro::MeshData` + `scene3d::ModelTable`). Por frame:

1. Se transforman y proyectan los vértices con la cámara del renderer (`math3d`, `project_camera_vertex`).
2. Cada cara visible se rasteriza por scanlines con un paso 16.16 por arista. Las caras traseras se descartan con el signo de la normal rotada respecto a la cámara.
3. Cada cara lleva una normal local normalizada que se calcula una vez al construir el modelo. En runtime se rota con la nave y se compara con la dirección al **sol común** (`kSunX/Y/Z`); el ángulo elige una de las **tres intensidades** de la gama de la nave. El jugador usa teal (claro/medio/oscuro) y los enemigos salmon/ladrillo/granate; la luz caliente 7 se comparte con motor, disparos y estrellas.
4. Los spans se escriben directamente en los planos dinámicos 0/1/2 con `plane_or_span` (color de 3 bits). El fondo/HUD vive en el plano 3 con índices 8..15 y no se toca por frame.
5. Los sólidos se ordenan de lejos a cerca (painter por `z`). Las estrellas se dibujan después de los sólidos y no se pintan encima de una nave; el fuego del motor es un segmento directo de 6 px.
6. La zona de vuelo (320x160) se limpia entera en los tres planos dinámicos; el triple buffer evita residuos entre frames.
7. El jugador y los enemigos aplican roll/pitch/yaw; los enemigos añaden un weave X/Y para que la trayectoria se lea aunque el modelo sea convexo y pequeño.

El HUD de host se refresca cada 16 frames: cuesta del orden de 25k ciclos cuando se dibuja. Las estrellas son 20 puntos 3D.

## Búsqueda previa

Antes de escribir esta ruta se comprobó qué ofrecía el engine para siluetas 3D y relleno de spans:

- `eng::graphics::render_convex_silhouette` (ruta de silueta por máscara + `Surface::fill_polygon`): válida y probada, pero en esta escena paga máscara 1bpp + cookie-cut por plano y rondaba 70-90k ciclos por objeto.
- `eng::playfield::Surface::fill_span` (ruta CPU por span): se descartó para las siluetas porque el coste fijo por llamada (~5k ciclos) dominaba; un parche de cabina de cinco filas costaba ~25k.
- `eng::graphics::detail::plane_or_span`: escritor directo del span planar, reutilizado en la demo. Se optimizó en el engine con una ruta por palabras para Amiga big-endian (la ruta por bytes se conserva en host little-endian); la equivalencia la cubren HOST-217/474/479/481/482.
- `eng::scene3d::ModelTable`/`Mesh` y `math3d::mesh_transform`/`project_camera_vertex`: modelo de malla y proyección reutilizados sin duplicar el renderer.
- La silueta de dos tonos (26,42 fps, commit `fd04dc87`) y la silueta de un color (28,48 fps) se midieron antes. La ruta de caras con `screen_area2`/`fill_screen_polygon` daba 16,87 fps; la versión actual con rasterizado directo de caras y OR de 3 bits sube a **19,57 fps**. Es la ruta visual completa sin BOBs, pero queda por debajo del suelo de 25 fps; el siguiente paso para recuperar presupuesto es pre-renderizar las caras como BOBs/sprites.

Los patrones de `planar_band.hpp`/`blob_batch.hpp`/`add_world_bitmap` no se usan: aquí no hay BOBs ni capas de actores, sino caras 3D proyectadas. La escena es pequeña y el presupuesto manda; un router general de rutas por objeto sigue pendiente en P8.

## Rendimiento y validación

- Release A500: **19,57 fps**, 362.399 ciclos/frame, 2,554 campos/frame; mínimo de ventana 18,9. **No cumple el suelo de 25 fps**; es una excepción documentada por el salto visual a caras reales con luz común (el usuario pidió priorizar las caras y commitear de todas formas). La variante de silueta de dos tonos a 26,42 fps queda en el historial (`fd04dc87`) por si se prefiere el presupuesto.
- Secuencia de 8 frames: 7/7 cambios, 0 frames estáticos (`verify-3d-sequence --require-motion --require-distinct`); áreas 73964, 74808, 75576, 73992, 74240, 72604, 72960, 73480.
- Flicker: gate OK; 16 candidatos / 52 bloques frente a la baseline 36/55.
- Visión final: `src/VISION.md` (prompt canónico, `qwen3-vl`). El modelo ve el objeto principal con **borde más oscuro** (caras), dos naves rojizas, movimiento entre frames y contornos limpios. La hoja de contacto 0/4/7 confirma movimiento, color propio por nave e intensidades distintas por cara. El HUD de fps que lee el modelo no es fiable (alucina valores); la medida real es `measure-fps`.
- Detalles, prompts crudos y ablaciones: [`VALIDATION.md`](VALIDATION.md); intención y elementos: [`src/INTENT.md`](src/INTENT.md).

## Archivos

- `src/main.cpp` - demo completa (modelos, rail, caras con luz, fondo y HUD).
- `src/INTENT.md` - qué se pretende ver y elementos esperados.
- `src/VISION.md` - evaluación canónica del modelo de visión.
- `VALIDATION.md` - medidas, prompts/respuestas y conclusiones.
- `flicker-baseline.json` - tolerancia calibrada con el movimiento legítimo.

## Límites conocidos

- Painter sin z-buffer: los sólidos que se intersectan podrían ordenarse mal; la escena por defecto los mantiene separados.
- Sin entrada ni colisión: es un traveling, no un juego.
- El campo de asteroides visible es estático (banda inferior). Los asteroides/anillos dinámicos están como opción de compilación (`kAsteroids`/`kGates`) pero desactivados para no romper el presupuesto.
- El render por caras es una ruta local de la demo; su promoción a un router de rutas del engine es deuda de P8/G3.
- El sombreado por caras en CPU no cabe en 25 fps con este tamaño de nave/pantalla: 19,57 fps. Para recuperar el presupuesto hay que pasar las caras a BOBs/sprites pre-renderizados o volver a la silueta de dos tonos (26,42 fps).
