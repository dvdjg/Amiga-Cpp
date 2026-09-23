# Política de cabeceras e implementación

Regla canónica de **qué va en cabecera (`engine/include/eng/`) y qué va en unidad de compilación (`engine/src/`)**. Complementa [`CODING_STYLE.md`](CODING_STYLE.md) y [`ENGINE_STRUCTURE_REVIEW.md`](ENGINE_STRUCTURE_REVIEW.md). Plan de organización general: [`../../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md`](../../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md).

## 1. Defecto: header-only

El engine es header-only **por diseño**, y eso es deliberado, no un accidente:

1. **Rendimiento m68k.** El vocabulario del engine se apoya en cadenas de `always_inline` (copper, blit, raster, scroll). Una frontera de unidad de compilación añade un `jsr` por llamada y rompe esas cadenas; el build lo documenta con la medición de `-Os` (`tools/build/build-demo.sh`).
2. **Freestanding sin librería.** Las demos no enlazan una `libeng`: el engine son cabeceras que se compilan con la demo.
3. **Testabilidad host gratis.** `tools/run-host-tests.sh` compila `tests/host/<categoría>/NNN/src/main.cpp` con `-I engine/include` y **no enlaza `engine/src`**. El código de dominio en cabecera es testeable sin configuración extra.

## 2. Cuándo usar `.cpp`

| Caso | Destino | Motivo |
|---|---|---|
| Plantilla / `constexpr` / tipo-valor pequeño | **Cabecera** | Necesita instanciación e inline. |
| Ruta caliente por frame (copper, blit, raster, scroll) | **Cabecera** (`inline`/`always_inline`) | El `jsr` se paga cada frame. |
| No-plantilla, **frío**, pesado y grande (búsqueda de tablero, tick de `sim`, decodificadores, E/S) | **`.cpp`** en `engine/src/<área>/` | No necesita inline; baja tiempos de compilación. |
| Backend de máquina | **`.cpp`** en `engine/src/platform/<familia>/` | Hardware, no plantilla. |

El build de demos compila `engine/src/**/*.cpp` (globbing), así que añadir unidades no requiere registrarlas.

## 3. Prerrequisito para `.cpp` en el dominio

Mover lógica de dominio a `.cpp` la deja **sin test host** mientras `run-host-tests.sh` no enlace `engine/src`. Antes de adoptarlo:

1. `tools/build/build-host-lib.sh` compila `engine/src/**/*.cpp` **excepto** backends de plataforma y produce `out/host-lib/libeng.a`.
2. `tools/run-host-tests.sh` enlaza esa lib.

Sin ese paso, una unidad de dominio en `.cpp` incumple la regla de cierre de [`../../testing/README.md`](../../testing/README.md).

## 4. Cabeceras gigantes: partir por tema, no por `.cpp`

Una cabecera de cientos de líneas se arregla **partiéndola por tema** en varias cabeceras con una **cabecera de familia** que las incluya (patrón de `ENGINE_STRUCTURE_REVIEW.md` §D3), no moviéndola a `.cpp`. Así se conserva el inline y no se rompen consumidores. Ya troceadas: `composition/compose.hpp` (`scene.hpp` + `stages.hpp`), `field/xlimited_playfield.hpp` (`xlimited_mapping.hpp`) y `sim/world.hpp` (`world_core.hpp`). Lo vigilan `tools/check/header-impl.mjs --strict` (falla por encima del umbral de líneas o por funciones no-`inline` a nivel de espacio de nombres) y `tools/check/engine-tree.mjs` (estructura temática de `eng/`).
