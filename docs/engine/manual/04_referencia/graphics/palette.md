# Referencia — paletas

El color es **RGB444** (12 bits). `palette.hpp` es la capa de **ergonomía** (el juego escribe `pal.set(...)`/`pal.fade(...)`); `palette32.hpp` es el **almacenamiento** (los 32 registros); la aritmética de color vive en `eng::util`.

```
   Color (RGB444) · ColorIndex (0..31)     ← tipos de dominio
   Palette (valor, 32 colores)             ← set/get/fill/fade/mix
        │ words() / operator PaletteWords
        ├─► Palette32 (almacenamiento)     ← base de escena EHB, fundido, ciclo
        └─► FramePlan::add_base_palette_patch   (apply)
```

## `Palette` y tipos de dominio — `palette.hpp`

| Tipo | Qué es |
|---|---|
| `ColorIndex` (`palette.hpp:33`) | Índice de registro físico (0..31); `color_index(i)` (`:39`) recorta a 31 (no hay índice inválido). |
| `Color` (`palette.hpp:44`) | Color RGB444 (`0x0RGB`); `rgb(r, g, b)` (`:51`) valida el rango 0..15, `from_raw(rgb444)` (se enmascara a 12 bits). |
| `Palette` (`palette.hpp:63`) | Valor de 32 colores con las operaciones de **intención** (`set`/`get`/`fill`/`fade`/`mix`) y una vía de publicación a un `FramePlan` (`apply`). **No** conoce registros de hardware ni `color_register`: la materialización la decide el driver (parche base del `FramePlan`, zona de Copper, doble buffer…). |

`fade`/`mix` son `constexpr`: se pueden evaluar en compilación.

## `Palette32` — `palette32.hpp`

`Palette32` (`palette32.hpp:29`) es el **almacenamiento** de los 32 registros: un array `color[kPaletteEntries]` (`kPaletteEntries = 32`, `:25`) con conversión a `PaletteWords` (`operator PaletteWords`/`words()`, `:33`) — la vista no propietaria que viaja por las APIs (Copper, drivers). `kBlackPalette` (`:42`) es la paleta negra por defecto. Es la base de escena EHB, el fundido y los efectos de ciclo.

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
