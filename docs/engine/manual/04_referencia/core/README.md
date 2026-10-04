# Referencia — `core/`

El módulo `eng::core` (`engine/include/eng/core/`) es la **base sin hardware** de la que dependen todos los demás: tipos y dominios, matemáticas genéricas, estructuras de datos y utilidades. No conoce el chipset ni el backend; es lo que hace que una cabecera del engine sea **genérica sobre el escalar** (`docs/engine/architecture/CODING_STYLE.md`).

## Submódulos

| Submódulo | Qué documenta |
|---|---|
| [`types/`](types.md) | Tipos base (`u8`…`u64`, `s8`…`s64`, `usize`), `Result`, `align_up`; dominios (`Tag`) y sus vistas (`Bytes`/`Words`/`ChipView`); `Span`; `Box`; `Address`/`MemoryKind`; `Ref`/`NonNull`; `Block`. |
| [`math/`](math.md) | Escalar (`scalar_traits`, `Vec`/`Mat`), `Fixed`, `MiniFloat16`, `SinTable`, ruido, interpolación. |
| [`data/`](data.md) | `ct_array` (arrays `constexpr`), orden de bytes, `crc32`, ordenación, `utf8`, `rtc`, 3D (`mesh3d`/`polygon`). |
| [`util/`](util.md) | `expected`, `static_vector`/`small_vector`, `pool`, `string_view`/`static_string`, `function_ref`, `scope_guard`, contenedores y algoritmos. |

## Regla de genericidad

Una cabecera del engine debe ser **genérica sobre lo que varía** (escalar, dimensión, capacidad, política) siempre que el algoritmo no dependa de un tipo concreto. El escalar (o cualquier representación concreta: `Fixed`, `MiniFloat16`, `float`) es **parámetro de plantilla**: la cabecera usa solo el **vocabulario genérico** (`scalar_traits`, `scalar_const`, `mul_norm`/`div_norm`, `fbm2`…) y **no** incluye el escalar concreto. El gate `tools/check/generic-headers.mjs` lo verifica (tipo concreto o include de escalar en cabecera genérica → falla); el test de una cabecera genérica la ejercita con **al menos dos escalares** (`AGENTS.md` §1.11).

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
