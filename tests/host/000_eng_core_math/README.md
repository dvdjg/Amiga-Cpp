# Test HOST-000: eng::core math (isqrt + sort + crc32 + random)

Test unitario **host** (compila con g++ del sistema, sin WinUAE) que valida los
primeros algoritmos portados de `demoscene-repo-orig/lib/libmisc` y
`libc/stdlib` a `eng::core`:

| Cabecera | Origen | Qué valida |
|---|---|---|
| [engine/include/eng/core/isqrt.hpp](../../../../engine/include/eng/core/isqrt.hpp) | `libmisc/fx.c` (`isqrt`) | `eng::isqrt` con equivalencia al C original (raíz por tabla con sesgo, no exacta). |
| [engine/include/eng/core/sort.hpp](../../../../engine/include/eng/core/sort.hpp) | `libmisc/sort.c` (`SortItemArray`) | `eng::quick_sort` genérico y `eng::sort_items` (orden por key). |
| [engine/include/eng/core/crc32.hpp](../../../../engine/include/eng/core/crc32.hpp) | `libmisc/crc32.c` | CRC-32 IEEE; equivalencia con el C original (y con el valor canónico de "123456789"). |
| [engine/include/eng/core/random.hpp](../../../../engine/include/eng/core/random.hpp) | `libc/stdlib/random.c` | xoroshiro64++ (la variante `swap` del origen equivale a `rotl32` estándar); equivalencia con el C compilado. |

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
  sesgo; p. ej. `isqrt(9)==2`, `isqrt(32768)==181`). El algoritmo original no
  es una raíz exacta y el port conserva ese comportamiento.
- `eng::crc32(data, len)`: CRC-32 IEEE estándar; valida contra el C original y
  contra el valor canónico `0xCBF43926` para "123456789".
- `eng::Xoroshiro64pp`: xoroshiro64++; la secuencia coincide con la del
  `random.c` del demoscene (cuyo `rol` por rangos+`swap16` equivale a `rotl32`).
- `eng::quick_sort(Span<T>, less)`: ordenación de un array de `s32`
  desordenado (verifica que queda ordenado y que el multiconjunto se conserva).
- `eng::sort_items(Span<SortItem>)`: ordena por `key` (el quicksort no es
  estable; el orden de los iguales no está garantizado, igual que en el
  original).

## Enlaces

- Roadmap de portación de librerías:
  [`../docs/demos/effects/LIBRARIES-CPP23-IMPORT-ROADMAP.md`](../../../docs/demos/effects/LIBRARIES-CPP23-IMPORT-ROADMAP.md).
- Repo origen: `C:\Users\dvdjg\Documents\programa\AI\Amiga\demoscene-repo-orig\lib\libmisc\`.