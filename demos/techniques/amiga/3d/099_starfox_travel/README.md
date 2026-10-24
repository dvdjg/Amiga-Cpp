# 099 - Star Fox style rail travel

Traveling **no jugable** inspirado en los niveles de espacio de Star Fox (SNES): un caza cian del jugador, dos cazas rojos que se acercan y se abren a los lados, disparos de ambos bandos, estrellas en movimiento y un fondo espacial con banda superior, planeta, luna y rocas grises. La cámara avanza por un rail fijo; todavía no hay entrada, colisión, puntuación ni audio.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release
```

El número 099 se eligió para no ocupar el 097 que el plan de presentación 3D reserva para la demo de horizonte Copper.

## Técnica

Los modelos (caza y enemigo) se construyen en código como sólidos convexos de 4-5 vértices (`retro::MeshData` + `scene3d::ModelTable`). Por frame:

1. Se transforman y proyectan los vértices con la cámara del renderer (`math3d`, `project_camera_vertex`).
2. Se calcula la envolvente convexa 2D con una cadena monótona y se rasteriza por scanlines con un paso 16.16 por arista.
3. Cada span se escribe directamente en los planos 0/1 con `plane_or_span`; el fondo/HUD viven en los planos 2/3 y no se tocan.
4. Los sólidos se ordenan de lejos a cerca (painter por `z`). La cabina y el fuego del motor son parches 2D de bajo coste, no sólidos adicionales.
5. La zona de vuelo (320x160) se limpia entera en los dos planos dinámicos; el doble/triple buffer evita residuos entre frames.
6. Las estrellas y las balas se proyectan y escriben directo en planos; las balas usan un rastro vertical fijo de 6 px porque la cola 3D se proyectaba como un punto cerca del horizonte.

El HUD de host se refresca cada 8 frames: cuesta del orden de 25k ciclos/frame y su actualización por frame tiraba la cadencia por debajo de 25 fps. El rótulo de la franja de bitplanes sigue siendo estático.

## Búsqueda previa

Antes de escribir esta ruta se comprobó qué ofrecía el engine para siluetas 3D y relleno de spans:

- `eng::graphics::render_convex_silhouette` (ruta de silueta por máscara + `Surface::fill_polygon`): válida y probada, pero en esta escena paga máscara 1bpp + cookie-cut por plano y rondaba 70-90k ciclos por objeto.
- `eng::playfield::Surface::fill_span` (ruta CPU por span): se descartó para las siluetas porque el coste fijo por llamada (~5k ciclos) dominaba; un parche de cabina de cinco filas costaba ~25k.
- `eng::graphics::detail::plane_or_span`: escritor directo del span planar, reutilizado en la demo. Se optimizó en el engine con una ruta por palabras para Amiga big-endian (la ruta por bytes se conserva en host little-endian); la equivalencia la cubren HOST-217/474/479/481/482.
- `eng::scene3d::ModelTable`/`Mesh` y `math3d::mesh_transform`/`project_camera_vertex`: modelo de malla y proyección reutilizados sin duplicar el renderer.

Los patrones de `planar_band.hpp`/`blob_batch.hpp`/`add_world_bitmap` no se usan: aquí no hay BOBs ni capas de actores, sino siluetas 3D proyectadas. La escena es pequeña y el presupuesto manda; un router general de rutas por objeto sigue pendiente en P8.

## Rendimiento y validación

- Release A500: **28,48 fps**, 249.041 ciclos/frame, 1,755 campos/frame; mínimo de ventana 27,3 (suelo 25 fps por ser relleno poligonal).
- Secuencia de 8 frames: 7/7 cambios, 0 frames estáticos (`verify-3d-sequence --require-motion --require-distinct`).
- Flicker: gate OK; baseline calibrada con los candidatos legítimos de los cazas/estrellas en movimiento.
- Visión final: `src/VISION.md` (prompt canónico, `qwen3-vl`). El modelo ve la nave y los cazas rojos moviéndose, contornos limpios y ningún artefacto; omite las balas pequeñas y a veces sobrecuenta los enemigos o describe las estrellas como fijas. La comprobación objetiva de la secuencia y el recorte ampliado del centro confirman los disparos.
- Detalles, prompts crudos y ablaciones: [`VALIDATION.md`](VALIDATION.md); intención y elementos: [`src/INTENT.md`](src/INTENT.md).

## Archivos

- `src/main.cpp` — demo completa (modelos, rail, render por spans, fondo y HUD).
- `src/INTENT.md` — qué se pretende ver y elementos esperados.
- `src/VISION.md` — evaluación canónica del modelo de visión.
- `VALIDATION.md` — medidas, prompts/respuestas y conclusiones.
- `flicker-baseline.json` — tolerancia calibrada con el movimiento legítimo.

## Límites conocidos

- Painter sin z-buffer: los sólidos que se intersectan podrían ordenarse mal; la escena por defecto los mantiene separados.
- Sin entrada ni colisión: es un traveling, no un juego.
- El campo de asteroides visible es estático (banda inferior). Los asteroides/anillos dinámicos están como opción de compilación (`kAsteroids`/`kGates`) pero desactivados para no romper el presupuesto.
- El render por spans es una ruta local de la demo; su promoción a un router de rutas del engine es deuda de P8/G3.
