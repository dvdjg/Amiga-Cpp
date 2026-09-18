# Demo 086: objetos de bitmap (BOB) por Blitter

Gate visual del camino de **BOB** del sistema de objetos
(`docs/engine/architecture/OBJECT_SYSTEM.md`): dibujar y **borrar** objetos con el Blitter a
través de la capa de actores (`actor_emit` + `bob.hpp`), que hasta ahora solo estaba cubierta
por tests host.

## Qué valida

Tres objetos con políticas distintas, sobre un fondo **estático** (no se repinta: un borrado
mal dimensionado deja rastro visible):

| Objeto | Transparencia | Borrado | Mecanismo |
|---|---|---|---|
| disco 32×32 | cookie-cut `$CA` + máscara | caja | `D = A·B + ¬A·C` (A=máscara, B=objeto, C=destino) |
| cuadrado 16×16 | OR aditivo `$FC` | caja | `D = A \| D`, sin máscara |
| cuadrado 16×16 | opaco `$F0` | save-under | `RestoreRect` + `CopyRect` + dibujo |

Los tres van con **desplazamiento fino** (X no múltiplo de 16), que usa el barrel shifter con la
palabra de guarda de la hoja, y recorren **bandas disjuntas** (las políticas de borrado por caja
y save-under solo son correctas si un objeto no invade la caja de otro). `g_eng_run_status.detail`
publica `actores << 8 | jobs`.

## Copper por objeto y a nivel de escena

El copper del frame lo compone `copper::Plan`:

- **Cielo**: degradado (RGB444 interpolado) con `K_086_SKY_BANDS` intenciones `PaletteLine` sobre
  `COLOR00`. Con `K_086_SKY_BANDS=256` es **un valor por línea de raster** (continuo, lo que se ve en
  las capturas); con 32 es en bandas. Las intenciones son **constantes** (`kSkyIntents`, `constexpr`).
- **Cada BOB declara sus necesidades de Copper** (`ActorDesc::copper`): un arcoíris de 4 pasos a lo
  largo de sus filas, expresado en líneas **relativas a su Y**. `actor_add_copper` las convierte a
  líneas absolutas y las añade al plan **con la prioridad `(superficie, z)` del actor**; el objeto no
  escribe registros en ningún momento. Trampa que costó encontrarlo: `emit_palette` recorta `count` a
  `colors.size() - first`, así que una intención con `first = 1` (COLOR01) necesita una vista de
  puestos `first + count` (aquí, 2); con 1 el scheduler emitía **cero** MOVEs y el objeto "pedía" en
  vano.

Configuración:

- `-DK_086_BOBS=n` (1..16; por defecto **3**).
- `-DK_086_SKY_BANDS=n` (por defecto **256**, un color por línea: degradado continuo).
- `-DK_086_STATIC_COPPER=1` (por defecto): el cielo es **constante**, así que la lista se construye
  **una vez** y no se re-emite cada frame; las necesidades **dinámicas** de los objetos se desactivan.
  `-DK_086_STATIC_COPPER=0` reactiva el copper por objeto (anclado a su Y), que exige
  re-materializar el plan cada frame.

## Coste medido (evidencia)

`fps` emulados con el contador de ciclos del Amiga (`tools/debug/measure-fps.mjs`); un campo PAL ≈
142.000 ciclos.

| Configuración | fps emulado | campos/frame |
|---|---|---|
| **3 BOBs + copper estático (por defecto)** | **49,92** | **1,0** |
| 8 BOBs, cielo por línea, copper **dinámico** | 7,13 | 7,0 |
| 8 BOBs, cielo por línea, copper dinámico **sin** re-emitir la parte estática | 24,88 | 2,0 |

La clave es **no re-emitir copper que no cambia**: con `K_086_STATIC_COPPER=1` la lista (display +
paleta + 256 intenciones del cielo) es idéntica cada frame, así que se construye una vez en `init` y
se salta `build_frame` en `update` (el perfil bajó `copper` de ~68k a ~0 y cruzó de 2 a 1 campo).

Con **copper dinámico** el framerate **no** llega a 50: los `actors` + `blits` de los BOB ya suman
más de un campo (≈236k ciclos con 8 BOBs) antes de contar el copper, así que ese modo queda como
referencia visual (degradado por objeto anclado a su Y), no como configuración de 50 fps.

## Defecto que encontró esta demo

Con el borrado implementado como «`words = ceil(ancho/16)`» quedaba un **residuo en el borde
derecho**: el dibujo procesa `base + 1` palabras cuando hay desplazamiento (barrel shifter), pero
el borrado y el save-under procesaban solo `base`, dejando hasta 15 px por fila sin limpiar (un
rastro de fragmentos que crecía por frame). Corregido: `bob_erase_box`, `emit_save` y
`emit_restore` calculan `words = base + (shift != 0)`. Cubierto por `072_actor`.

Una sonda de diagnóstico útil: pintar el fondo con un índice distinto de 0 hace **visible** dónde
y cómo borra el Blitter (un `ClearRect` deja índice 0); sirvió para separar «no borra» de «borra
dejando residuo».

## Verificación de la librería de utilidades

El camino de actores usa `eng::util::BitSet` (`ActorStore`: slots vivos del parque generacional) y `eng::util::StaticVector` (`emit_bob_fallbacks`: degradados a BOB), y el gradiente del cielo usa `eng::util::lerp444` (RGB444), así que esta demo es su verificación por demo (ver `docs/engine/architecture/TEMPLATE_LIBRARY.md`). El analizador propio `analyze-screenshot.sh` exige verde y amarillo (el arcoíris de los objetos) y valida el `run-report` en Ready, en lugar del overlay de depuración genérico.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/086_bob_objects --clean
tools/run/run-demo.sh       demos/amiga/086_bob_objects
```
