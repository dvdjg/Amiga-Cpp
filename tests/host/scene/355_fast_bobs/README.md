# HOST-355 - fast_bobs

Test de la capa de **Fast BOBs** (`eng/scene/bobs.hpp::FastBobLayer`): BOBs sobre un playfield
frontal vacío dibujados con **copia + padding** (minterm `$F0`, 2 canales DMA), con degradación
automática a **clear del área previa + cookie-cut** (`$CA`) en los casos que la copia no cubre.

## Qué cubre

- **Camino rápido**: sin historial ni solape, cada actor emite **una** copia opaca (`OrBlob`,
  `$F0`) cuyo destino incluye el padding (`x - pad`).
- **Degradación por movimiento**: si el actor se mueve más que el padding, la capa emite un
  `ClearRect` del área previa y dibuja con cookie-cut; los demás siguen por el camino rápido.
- **Degradación por solape**: si dos actores comparten área (unión de lo pintado antes y lo que
  pintarán), ambos degradan a cookie-cut (la copia de uno borraría al otro).
- **Actor oculto**: no se pinta.

La ficha de la técnica está en
[`docs/reference/amiga/techniques/dual-playfield-fastbobs.md`](../../../../docs/reference/amiga/techniques/dual-playfield-fastbobs.md).

## Cómo se ejecuta

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/355_fast_bobs
```
