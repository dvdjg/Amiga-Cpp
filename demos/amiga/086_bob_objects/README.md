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
  las capturas); con 32 es en bandas.
- **Cada BOB declara sus necesidades de Copper** (`ActorDesc::copper`): un arcoíris de 4 pasos a lo
  largo de sus filas, expresado en líneas **relativas a su Y**. `actor_add_copper` las convierte a
  líneas absolutas y las añade al plan **con la prioridad `(superficie, z)` del actor**; el objeto no
  escribe registros en ningún momento. El efecto es **visible**: el color del objeto cambia por filas.
  Trampa que costó encontrarlo: `emit_palette` recorta `count` a `colors.size() - first`, así que una
  intención con `first = 1` (COLOR01) necesita una vista de puestos `first + count` (aquí, 2); con 1
  el scheduler emitía **cero** MOVEs y el objeto "pedía" en vano.

Configuración: `-DK_086_BOBS=n` (1..16; rejilla 4×4 de celdas 80×64, los recorridos no se salen de su
celda) y `-DK_086_SKY_BANDS=n` (por defecto **256**, un color por línea: degradado continuo).

## Coste medido (evidencia)

| Configuración | fps emulado | campos/frame |
|---|---|---|
| 3 BOBs, lista de copper estática (revisión anterior) | **50,09** | 1,0 |
| 8 BOBs, cielo **por línea** (256 intenciones) | **5,47** | 9,1 |
| 8 BOBs, cielo en 32 bandas | **12,56** | 4,0 |

El coste **no** escala con el número de intenciones de forma lineal (64 intenciones ya cuestan 4
campos) y no lo explican ni el copper ni los blits: es el **bucle** del frame (F4.6 del roadmap, que
sigue pendiente). Por eso el modo por línea **no cumple 50 fps** y queda como referencia visual, no
como configuración por defecto. Además, con 8 BOBs `detail` marca 7 emitidos de 8: hay un actor que
devuelve no-`Ok` y está por diagnosticar.

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

El camino de actores usa `eng::util::BitSet` (`ActorStore`: slots vivos del parque generacional) y `eng::util::StaticVector` (`emit_bob_fallbacks`: degradados a BOB), así que esta demo es su verificación por demo (ver `docs/engine/architecture/TEMPLATE_LIBRARY.md`). El analizador propio `analyze-screenshot.sh` exige verde y amarillo (el arcoíris de los objetos) y valida el `run-report` en Ready, en lugar del overlay de depuración genérico.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/086_bob_objects --clean
tools/run/run-demo.sh       demos/amiga/086_bob_objects
```
