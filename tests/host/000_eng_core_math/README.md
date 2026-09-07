# Test HOST-000: eng::core math (isqrt + sort)

Test unitario **host** (compila con g++ del sistema, sin WinUAE) que valida los
primeros algoritmos portados de `demoscene-repo-orig/lib/libmisc` a `eng::core`:

| Cabecera | Origen | Qué valida |
|---|---|---|
| [engine/include/eng/core/isqrt.hpp](../../../../engine/include/eng/core/isqrt.hpp) | `libmisc/fx.c` (`isqrt`) | `eng::isqrt` con cuadrados exactos y la propiedad floor de la raíz. |
| [engine/include/eng/core/sort.hpp](../../../../engine/include/eng/core/sort.hpp) | `libmisc/sort.c` (`SortItemArray`) | `eng::quick_sort` genérico y `eng::sort_items` (orden por key). |

Al ser algoritmos puros (freestanding), se validan en host de forma rápida y
determinista, en el mismo compilador GCC que usa el toolchain Amiga. No
necesitan canal lateral ni `g_eng_run_status`.

## Ejecución

Desde la raíz del repo:

```bash
bash tools/run-host-tests.sh                                  # todos
bash tools/run-host-tests.sh tests/host/000_eng_core_math      # solo este
```

Salida: `OK: todas las comprobaciones pasaron` y código de salida 0 (éxito).
Cualquier aserción fallida imprime `[FAIL]` y hace que el binario devuelva
distinto de 0.

## Qué ejercita

- `eng::isqrt(n)`: **equivalencia con el C original** (aprox. por tabla con
  sesgo; p. ej. `isqrt(9)==2`, `isqrt(32768)==181`). Las muestras se
  autenticaron compilando el `fx.c` de origen contra `eng::isqrt`; el algoritmo
  original no es una raíz exacta y el port conserva ese comportamiento.
- `eng::quick_sort(Span<T>, less)`: ordenación de un array de `s32`
  desordenado (verifica que queda ordenado y que el multiconjunto se conserva).
- `eng::sort_items(Span<SortItem>)`: ordena por `key` (el quicksort no es
  estable; el orden de los iguales no está garantizado, igual que en el
  original).

## Enlaces

- Roadmap de portación de librerías:
  [`../docs/demos/effects/LIBRARIES-CPP23-IMPORT-ROADMAP.md`](../../../docs/demos/effects/LIBRARIES-CPP23-IMPORT-ROADMAP.md).
- Repo origen: `C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo-orig\lib\libmisc\`.