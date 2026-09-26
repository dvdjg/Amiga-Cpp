# Batería de tests HOST del engine (algoritmos y APIs puras)

Estos tests compilan **con g++ del host** (el compilador GCC del entorno de
desarrollo del toolchain Amiga, no MSVC) contra `engine/include` y se ejecutan
como un binario nativo de la máquina de desarrollo. Sirven para validar
**algoritmos y APIs puras** (matemáticas, ordenación, tablas, etc.) que no
dependen de hardware y no necesitan WinUAE.

> **Motivación.** El engine se compila para un cruce `m68k-*` (Bartman/VSCode o
> el toolchain del proyecto), pero las cabeceras de `eng/core` son freestanding:
> no usan STL ni dependen del backend. Probarlas con g++ del host es lo más
> rápido y determinista para detectar regresiones en algoritmos puros.

## Dónde está y cómo corre

- Los tests viven en `tests/host/<categoría>/NNN_<nombre>/` con `src/main.cpp`, `README.md`.
- Se compilan con `tools/run-host-tests.sh` (usa `g++` del PATH o la variable
  `CXX`).
- No necesitan `g_eng_run_status` ni canal lateral: terminan con código de
  salida 0 (OK) o distinto de 0 (fallo) y escriben un informe por stdout.

## Convenciones

- El código de los algoritmos que se validan vive en `engine/include/eng/core/`
  y es freestanding (sin STL). Solo el `main.cpp` del test puede usar `printf`
  del host para informar.
- Cada test declara al principio las funciones/buenas prácticas que ejercita.
- Falla con mensaje claro si una aserción no se cumple; el script de regresión
  puede invocar estos binarios y considerar fallo `exit != 0`.
- **Numeración única y no reutilizable.** El prefijo `NNN` de `tests/host/<categoría>/NNN_<nombre>/` es único en todo `tests/host/`: un test nuevo toma el **siguiente número libre** (máximo + 1). Si se descubre una colisión, se renumera el test **más nuevo** (el que aún no estaba en el catálogo ni referenciado) al siguiente libre; nunca se cambia el número de un test ya catalogado. Tras renumerar hay que actualizar el título y las rutas dentro del test, sus referencias (p. ej. en `engine/`) y el catálogo de su categoría. Lo valida `tools/check/test-numbering.mjs` (corre en `tools/run-host-tests.sh`): falla si hay prefijos duplicados o si los catálogos no cuadran con los directorios.

## Cómo añadir un test

1. Elige la **categoría** y el **siguiente número libre** en `tests/host/<categoría>/NNN_<nombre>/` (máximo + 1).
2. Escribe `src/main.cpp` que `#include <eng/core/...>` y aserta los valores.
3. Añade el `README.md`.
4. Regístralo en el `README.md` de su categoría (catálogo) y, si la API que
   cubre sube a `engine/`, enlázalo también desde el doc-map del sistema.

## Categorías

| Categoría | Tests | Catálogo |
|-----------|-------|----------|
| `ai` | 18 | [ai/README.md](ai/README.md) |
| `audio` | 15 | [audio/README.md](audio/README.md) |
| `board` | 20 | [board/README.md](board/README.md) |
| `cards` | 12 | [cards/README.md](cards/README.md) |
| `core` | 73 | [core/README.md](core/README.md) |
| `field` | 25 | [field/README.md](field/README.md) |
| `graphics` | 33 | [graphics/README.md](graphics/README.md) |
| `os` | 19 | [os/README.md](os/README.md) |
| `parallel` | 1 | [parallel/README.md](parallel/README.md) |
| `platform/amiga` | 8 | [platform/amiga/README.md](platform/amiga/README.md) |
| `res` | 5 | [res/README.md](res/README.md) |
| `scene` | 3 | [scene/README.md](scene/README.md) |
| `sim` | 34 | [sim/README.md](sim/README.md) |
| `ui` | 18 | [ui/README.md](ui/README.md) |

Total: 271 tests. Los IDs `HOST-NNN` son únicos en todo `tests/host/` (no por categoría).
