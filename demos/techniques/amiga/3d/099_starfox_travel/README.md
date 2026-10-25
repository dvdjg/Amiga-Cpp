# 099 - Star Fox style rail travel

Traveling **no jugable** inspirado en Corneria (primer nivel de Star Fox SNES): vuelo rasante sobre agua y terreno verde, cielo de día, un Arwing blanco/azul, dos cazas enemigos rojos, un arco de piedra y un edificio. La cámara viaja por una trayectoria zigzagueante con yaw/pitch/roll suaves. No hay entrada, colisión, puntuación ni audio.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release
```

El número 099 se eligió para no ocupar el 097 que el plan de presentación 3D reserva para la demo de horizonte Copper.

## Técnica

Todos los **objetos** son 3D: Arwing, enemigos, arco y edificio. El cielo/terreno/agua son el fondo de la escena.

- El fondo tiene **tres variantes pre-rotadas** (roll -6°, 0°, +6°) en Chip RAM. Se elige la más cercana al roll de cámara y se copia al buffer trasero con el Blitter; la ventana de 320 px se desplaza en X (yaw) y en Y (pitch). El horizonte se inclina de verdad, no solo se desplaza.
- El Arwing usa 23 vértices/31 triángulos (morro, fuselaje, cabina elevada azul, alas y G-Diffusers); el enemigo, 11/14. Los quads se triangulan para que el relleno por scanlines sea convexo. Las normales se orientan por el centro del modelo, corrigiendo las alas izquierda/derecha.
- El arco y el edificio son sólidos 3D con caras n-gon y sombreado por el sol común; se ordenan por `z` y se pintan con spans CPU opacos de 4 planos.
- La cámara añade yaw/pitch en la copia del fondo, roll real en las tres variantes y roll/yaw/pitch en los objetos 3D. El jugador combina dos senos en X (zigzag) con una oscilación suave en Y.
- El HUD de bitplanes (título y `NO INPUT`) queda fijo; el HUD de host se refresca cada 16 frames.

Para revisar el Arwing se puede compilar con `EXTRA_DEFINES=-DK099_DEBUG_ARWING=1`: coloca al jugador en vista 3/4, sin enemigos, para inspeccionar morro/cabina. Captura de referencia: `out/vision-review/099_arwing_3q.png`.

## Búsqueda previa

- `eng::graphics::detail::plane_or_span`: spans planares por palabras en Amiga big-endian y por bytes en host (HOST-217/474/479/481/482).
- `eng::scene3d::ModelTable`/`Mesh`, `math3d::mesh_transform` y `project_camera_vertex`: malla, transformación y proyección reutilizadas.
- `Surface::fill_polygon`/`fill_rect`: se usaron para generar los fondos pre-rotados. El repintado completo por frame costaba ~10M ciclos; la copia del fondo estático con variantes es la ruta actual.
- La arquitectura 2D de versiones anteriores (arco/skyline/rocas pintados) se sustituyó por arco y edificio 3D. El coste medido es **3,65 fps**; se aceptó el intercambio por “todo 3D”.

## Rendimiento y validación

- Release A500: **3,65 fps**, 1.943.341 ciclos/frame, 13,697 campos/frame; mínimo de ventana 3,1. **No cumple el objetivo >6 fps**; es la excepción aceptada para tener arco y edificio 3D reales. La variante con arquitectura estática medía 7,36 fps.
- Secuencia de 8 frames: 7/7 cambios, 0 frames estáticos.
- Flicker: gate OK; 0 candidatos, 166 bloques frente a baseline 220 (el movimiento de cámara/fondo es coherente).
- Visión final: `src/VISION.md` (prompt canónico, `qwen3-vl`). El modelo ve el bloque/edificio gris, el arco, la nave azul/blanca, el horizonte inclinado y movimiento; contornos limpios.
- Detalles y medidas: [`VALIDATION.md`](VALIDATION.md); intención: [`src/INTENT.md`](src/INTENT.md).

## Archivos

- `src/main.cpp` - modelos 3D, fondos pre-rotados, cámara, disparos y HUD.
- `src/INTENT.md` - qué se pretende ver y elementos esperados.
- `src/VISION.md` - evaluación canónica del modelo de visión.
- `VALIDATION.md` - medidas, secuencia, visión y conclusiones.
- `flicker-baseline.json` - tolerancia calibrada con el movimiento legítimo.

## Límites conocidos

- **No cumple 6 fps**: 3,65 fps con dos objetos de arquitectura 3D. El renderer CPU por caras no da para más en este A500.
- El cielo, el terreno y el agua son el fondo de la escena; el arco, el edificio y las naves son los objetos 3D. No hay terreno 3D poligonal.
- Painter por objeto/cara, sin z-buffer. La escena mantiene los objetos separados.
- Sin entrada ni colisión: es un traveling, no un juego.
