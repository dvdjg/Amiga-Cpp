# Referencia — `core/data/`

Arrays y tablas `constexpr`, orden de bytes, checksums, ordenación, texto y 3D. Todo **sin heap** y apto para calcularse en compilación (los datos quedan en `.rodata`, no en el arranque).

## `ct_array` — `ct_array.hpp`

`ct_array<T, N>` (`ct_array.hpp:23`) es un array de tamaño `N` inicializable con una **lambda `constexpr`** índice a índice; es la forma idiomática del engine de generar tablas (p. ej. `mf16_rcp`). Sustituye a `std::array` donde hace falta que el contenido sea `constexpr`.

## Orden de bytes — `byte_order.hpp`

Lecturas/escrituras **explícitas** de endianness, para no depender del host: `read_be16`/`read_be32`/`write_be16`/`write_be32` (`byte_order.hpp:18`) y `read_le16`/`read_le32` (`byte_order.hpp:43`). El 68000 es big-endian y el host de test suele ser little-endian; usar estos helpers garantiza el mismo resultado en ambos.

## Checksums — `crc32.hpp`

`crc32(data, len)` (`crc32.hpp:77`) sobre la tabla `constexpr` `kCrc32Table` (`crc32.hpp:25`): integridad de assets/ficheros.

## Ordenación — `sort.hpp`

Algoritmos genéricos sobre `Span<T>` con un `Less`: `insertion_sort` para tramos pequeños (`sort.hpp:36`), `quick_sort` (`sort.hpp:64`), `stable_sort` (con `scratch`, `sort.hpp:143`), `nth_element` (`sort.hpp:193`), `partial_sort` (`sort.hpp:234`), `is_sorted` (`sort.hpp:249`) y `radix_sort_u16` (`sort.hpp:263`). `SortItem`/`sort_items` ordenan por clave (`sort.hpp:25`).

## Texto — `utf8.hpp`

`decode`/`encode` de code points UTF-8 (`utf8.hpp:26`, `:47`) y el patrón `lit<"texto">` que **decodifica en compilación** a anchos fijos (`utf8.hpp:90`), para textos sin coste en runtime.

## Tiempo real — `rtc.hpp`

`TimeOfDay` (`rtc.hpp:20`) y `from_tod(tod, hz)` (`rtc.hpp:30`): hora de pared desde el contador TOD del hardware.

## 3D — `mesh3d.hpp`, `polygon.hpp`

`MeshView` (`mesh3d.hpp:170`) da la vista de una malla: caras (`Face`/`FaceSpan`, `mesh3d.hpp:72`), área con signo (`face_signed_area`, `mesh3d.hpp:131`), visibilidad de cara (`face_visible`, `mesh3d.hpp:138`) y claves de profundidad `face_z_sum`/`face_z_min` (`mesh3d.hpp:145`) para ordenar. Todo con `mesh_traits<S>` (genérico sobre el escalar). `polygon.hpp` añade el trazado de polígonos (`math3d`).

## `binary.hpp`

Helpers de serialización/descodificación de estructuras binarias.

Volver al [índice de `core/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
