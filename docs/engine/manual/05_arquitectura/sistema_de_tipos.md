# Sistema de tipos

El engine evita el `void*`/`u8*` suelto y los números mágicos con un **sistema de tipos por etiqueta
(«tag»)**: cada buffer, cada dirección y cada unidad lleva **qué es**, no solo cuánto mide. La referencia
de diseño es `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md`.

## El problema que resuelve

```
  SIN tags  (peligroso)                 CON tags  (el engine)
  void*  p;                             Block<PlaneTag>  p;   // planos
  u16*   q;                             Bytes<BobTag>    q;   // hoja de BOB
  p = q;        // compila, ¡error!     p = q;   // NO compila: dominios distintos
```

Pasar un buffer de audio donde va un plano, o un color donde va un canal, **no compila**: el tipo lo
impide.

## Elementos

| Tipo | Qué es | Dónde |
|---|---|---|
| `Tag` | Marca de **dominio** (`PlaneTag`, `BobTag`, `ChunkyTag`, `CopperTag`, `SpriteTag`, `SimTag`…) | `core/types/domains.hpp` |
| `Bytes<Tag>` / `Words<Tag>` | Vista de bytes/palabras **con** dominio | `core/types/typed.hpp` |
| `MemView<Tag,Kind>` | Vista de memoria **con banco** (Chip/Fast/Slow) | `core/types/typed.hpp` |
| `Block<Tag>` | Reserva **propietaria** (base + tamaño + banco efectivo) | `core/types/typed.hpp` |
| `Address<Kind>` | Dirección tipada por banco (no un entero) | `core/types/typed.hpp` |
| `Box` | Rectángulo entero (x, y, w, h) | `core/types/box.hpp` |
| `Span<T>` | Rango contiguo no propietario | `core/types/span.hpp` |
| `Ref<T>` / `NonNull<T>` | Observador no propietario | `core/types/ptr.hpp` |
| `ColorIndex`, `LayerId`, `SpriteId`… | Unidades fuertes | `api/*` |

## `Block<Tag>` y la frontera

```
  Block<PlaneTag> b
  ┌───────────────────────────────────────────────┐
  │  b.view      ──► Bytes<PlaneTag>  (vista)      │  ← lo normal
  │  b.mem_view() ─► MemView<PlaneTag, Chip>       │  ← para el DMA/Blitter
  │  b.kind      = Chip (banco efectivo)           │
  │  b.raw() / block.view.raw()                    │  ← FRONTERA (puntero crudo)
  └───────────────────────────────────────────────┘
```

El **puntero crudo** (`u8*`/`u16*`) existe **solo en la frontera** con el hardware (registrar en el
Blitter, montar una copperlist). El engine la aísla en `raw()`, **documentada**; el código de juego no
la usa. El gate `tools/check/raw-pointer-members.mjs` impide punteros crudos a **objeto** sin
justificar.

## Genérico sobre el escalar

Una cabecera de algoritmo **no impone el escalar**: es **parámetro de plantilla** y el algoritmo usa el
**vocabulario genérico** (`scalar_traits`, `scalar_const`, `mul_norm`/`div_norm`, `fbm2`, `worley2`…):

```cpp
template <class S>                 // S = Fixed<…>, MiniFloat16, float, …
eng::Vec<2,S> normalize(eng::Vec<2,S> v) { ... }
```

El consumidor elige `S`; el host/test instancia la misma cabecera con **dos escalares** distintos
(regla de genericidad, `docs/engine/architecture/SCALAR_LIBRARY.md`; gate
`tools/check/generic-headers.mjs`).

## Por qué importa

- **Seguridad por construcción**: no puedes confundir un plano con una hoja.
- **Sin coste**: los tags son tipos vacíos; `Bytes<Tag>` no añade campos sobre un `u8*`.
- **Diagnóstico**: un `Block<Tag>` recuerda en qué banco quedó (clave para el C2P: «los tags eligen el
  método», ver [modelo de memoria](modelo_de_memoria.md)).

Volver a [Arquitectura](README.md) · [índice del manual](../README.md).
