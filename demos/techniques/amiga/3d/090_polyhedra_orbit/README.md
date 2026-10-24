# 090_polyhedra_orbit — icosidodecaedro y cubo en órbita

Demo de presentación 3D con dos sólidos convexos: un **icosidodecaedro** (30 vértices, 60 aristas, 32 caras: 20 triángulos y 12 pentágonos) girando sobre su centro y un **cubo** que lo orbita en una elipse mientras gira sobre sí mismo. Los tres ángulos de Euler de cada sólido derivan lentamente, de modo que sus ejes de rotación van cambiando.

El relleno es la técnica clásica de sólidos convexos del Blitter Amiga, generalizada a **colores por cara**: por cada plano se traza solo la frontera de la unión de caras que tienen ese bit (`EOR` + `ONEDOT`, las aristas internas se cancelan por paridad) y un **area fill `XOR`** reconstruye el índice de cada cara. No hay z-buffer ni painter por cara: las siluetas de los dos sólidos no se solapan (lo verifica `orbit_separation_ok()` al arrancar) y cada sólido se rellena con su propia caja.

- **4 planos (16 índices)**. Los códigos de cara usan 3 bits con paridad par en los triángulos (`3/5/6`) e impar en los pentágonos (`1/2/4/7`): cada arista triángulo-pentágono difiere en 1 o 3 bits (media 1,5) y nunca hay dos caras adyacentes con el mismo color. El plano 3 es la **capa de entramado**: lo tienen los triángulos, así que selecciona su unión; tras el relleno se le aplica un damero 2×2 (`0xAAAA/0x5555`, dos filas) y cada triángulo alterna entre su tono base y el compañero `+8`. Los pentágonos quedan sólidos.
- **Tabla de adyacencia** construida en `init` (60 aristas del poliedro + 12 del cubo, cada una con sus dos caras). Por frame solo se recorre esa tabla: el color de una arista es `colorA ^ colorB` si las dos caras son visibles, o el color de la cara visible en la silueta. Sin recortes ni deduplicación por plano.
- **Culling 2D** por el signo del área proyectada (con y hacia abajo, una cara exterior CCW da área negativa), en vez del producto mixto 3D. **Clear por sólido** (no la unión de ambos rectángulos) y **area fill por plano acotado a la caja** de cada sólido: el hueco entre el poliedro y el cubo no se barre.
- La palabra de descarte del primer píxel de cada línea (`line_d_base`) evita el punto suelto que el modo línea deja en la fila 0 al usar la base del bitmap.

## Búsqueda previa

Antes de escribir la demo se comprobó qué existe ya en el engine para no duplicar (regla §1.7):

- **Contorno + area fill XOR de sólidos convexos**: `eng::retro::flat_shade_xor` (`engine/include/eng/retro/flat_shade_xor.hpp`), la misma ruta de la demo 116. Se le añadieron dos parámetros opcionales: `per_plane_fill` (fill por plano acotado en ancho y alto, para objetos pequeños) y `line_d_base` (palabra de descarte del primer píxel).
- **Aplicación de patrón por plano**: no había método público para el paso `D = A & B` después de un relleno; se expuso `AmigaBackend::blitter_apply_pattern` reutilizando el helper interno `blit_apply_pattern_phased`. El catálogo de patrones vive en `eng::graphics::fill_pattern` (`FillPattern`, `fill_pattern_plane_cpu`).
- **Proyección y cámara**: `eng::graphics::project_camera_vertex`, `Camera3D`, `eng::math3d::mesh_transform`/`load_rotate`.
- **Composición de display y HUD**: `eng::graphics::composition` (`Scene`, `planar`, `palette`) y `eng::playfield::Surface::draw_text_words` con `Font8`.
- No se usan los patrones de bajo nivel vigilados por el gate `prior-art` (`planar_band.hpp`/`blob_batch.hpp`/`add_world_bitmap`): no hay objetos retenidos.

## Medidas (A500, configuración debug, PAL)

| Versión | Frame (ciclos) | Campos | fps HUD |
|---|---|---|---|
| Contorno por plano de 5 planos (antes) | ~1.55 M | ~10.9 | 3,9 |
| Silueta XOR + 4 planos + damero (actual) | 368–428 k de update; ~435–466 k totales | 3,07–3,28 | **15,3–16,2** |
| A/B: 2 colores sólidos sin damero | 368 k de update; 435 k totales | 3,07 | 16,3 |

Desglose del perfil (build con damero): `Project` 132 k (transform 66 k + culling/ensamblado 61 k), `Fill` 278 k (clear 28 k, contorno ≈143 k, area fill ≈30 k, damero 16 k, espera 11 k), `HUD` 6,8 k (media con refresco cada 32 frames). El contorno son ~80 operaciones de línea; el A/B sin damero baja a 184 k de contorno y aun así se queda en ~16,3 fps: el techo está en los blits de línea y en el transform del 68000, no en los colores.

**Estado**: la demo se queda en **3 campos (16,7 fps nominales, 15,3-16,2 medidos)**. El objetivo de **25 fps** sigue abierto: exigiria bajar de ~283 k totales (el bucle anade ~67 k de espera de VBlank) y no se alcanza con dos solidos y este numero de colores; la via probada de 3 planos no cambio el coste.

## Flags de compilación

- `K090_CPU_FILL=0` (por defecto): silueta XOR con el Blitter. `1` = spans de CPU, `2` = cookie-cut por cara, `3` = máscara por plano (rutas A/B de diagnóstico; no aplican el damero).
- `K090_DITHER=1` (por defecto): damero 2×2 en el plano de entramado. `0` deja los triángulos sólidos.
- `K090_CULL2D=1` (por defecto): culling por área proyectada. `0` usa la normal 3D.
- `K090_FREEZE=1` y `K090_START_PHASE=<0..4095>`: pose fija para capturas deterministas.
- `K090_SKIP` no existe: las sondas de coste se hacen con los flags anteriores y el perfilador.

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/090_polyhedra_orbit --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/090_polyhedra_orbit --settle-ms 7000 --sequence-frames 4
node ./tools/debug/profile.mjs 090_polyhedra_orbit A500_debug 4 --names Frame,Project,Fill,Hud,Clear,Lines,Pattern,Wait,Xform,Assemble
node ./tools/analyze/vision-final.mjs 090_polyhedra_orbit out/run/090_polyhedra_orbit/A500_debug/sequence --demo-dir demos/techniques/amiga/3d/090_polyhedra_orbit
```

## Documentación de la demo

- `src/INTENT.md` — qué hace, qué demuestra, elementos y evolución temporal.
- `src/VISION.md` — evaluación con el modelo de visión local (prompt canónico).
- `VALIDATION.md` — evidencia de geometría, medidas, visión, límites y deuda.
