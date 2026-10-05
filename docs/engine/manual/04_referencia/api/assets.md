# Referencia — `assets` (`eng/api/assets.hpp`)

`eng::Assets` es un **registro por nombre** de assets cocinados: al dar de alta un blob lo **copia a
Chip** (DMA) según su `Tag`, y lo entrega como objeto de **dominio** (`MusicModule`, `Sprite`, bytes,
paleta). El juego no ve `Block<Tag>` ni geometría de audio.

> **Nota de capas**: `assets.hpp` incluye `eng/audio/music_player.hpp` (asm de Amiga) → **no** se
> incluye desde `api.hpp`; las apps Amiga lo incluyen directamente.

## Alta y reserva

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `bind` | `void bind(MemoryManager&)` | gestor de memoria | — (llamar antes de `add`/`create`). |
| `add<Tag>` | `bool add(name, const u8* data, usize size)` | nombre, datos, tamaño | `false` si no cabe o no hay gestor. Copia a Chip. |
| `add_checked<Tag>` | `Expected<void,Result> add_checked(name, data, size)` | — | `InvalidArgument` / `OutOfMemory` / `HardwareLimit`. |
| `create<Tag>` | `Block<Tag> create(u32 bytes)` | bytes | bloque **dueño** (banco por dominio; Chip para DMA). |
| `create_checked<Tag>` | `Expected<Block<Tag>,Result> create_checked(u32 bytes)` | — | bloque o causa. |
| `release<Tag>` | `void release(const Block<Tag>&)` | bloque de `create` | — (lo devuelve al banco). |
| `reset_phase` | `void reset_phase()` | — | — (libera todos los bloques en **orden inverso**). |

## Objetos de dominio

| Método | Firma | Devuelve |
|---|---|---|
| `bitmap`/`add_bitmap` | `bool add_bitmap(name, data, size, w, h, planes, layout)` / `ChipBitmapView<PlaneTag> bitmap(name)` | vista planar lista para `screen.bitmap(...)`. |
| `add_sprite`/`sprite` | `bool add_sprite(name, data, size, const Bob&)` / `Sprite sprite(name[, const Bob&])` | hoja de sprites para `screen.sprite(...)`. |
| `music` | `audio::MusicModule music(name)` | módulo para `app.audio().play_music(...)`. |
| `bytes`/`palette` | `Span<const u8> bytes(name)` / `Span<const u16> palette(name)` | datos crudos / palabras COLOR. |
| `has`/`count`/`valid_view`/`tracked_count` | consultas | — |

```cpp
INCBIN(abyss_mod, "assets/amiga/audio/testmod.p61");
eng::Assets assets {app.memory_manager()};
assets.add<eng::MusicTag>("mod", incbin_abyss_mod_start, INCBIN_SIZE(abyss_mod));
app.audio().play_music(assets.music("mod"));
```

> Fuente: `engine/include/eng/api/assets.hpp`. Roadmap: `docs/guides/roadmap/ROADMAP_GAME_API.md` §4.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
