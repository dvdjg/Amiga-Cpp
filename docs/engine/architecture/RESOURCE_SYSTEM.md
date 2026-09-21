# Sistema de recursos: caché de assets y código dinámico (`eng::res`)

Gestión de recursos sobre la E/S asíncrona del mini-SO ([`MINI_OS_IO.md`](MINI_OS_IO.md)): una
**caché de assets** con presupuesto, prioridad y desalojo LRU, y un **loader de código relocatable**
(librerías cargables/descargables). La app pide recursos por **id** y sigue; cuando no caben, los
que no se usan y no son prioritarios **salen solos**. Es lo que permite moverse entre pantallas
(zonas) sin cargar el juego entero en RAM.

```text
  App / Game / Audio / UI
          │
          ▼
  ┌───────────────────┐
  │  AssetCache       │  refcount + prioridad + LRU, carga async
  ├───────────────────┤
  │  DynLoader        │  .englib / overlays relocables, load/unload
  ├───────────────────┤
  │  Vfs / File IO    │  open/read/write/create/delete + FileDone   (MINI_OS_IO.md)
  └─────────┬─────────┘
            ▼
      MsgPort (FileDone, AssetReady, AssetEvicted, LibLoaded…)
```

En A500 la caché **no** es "todo el disco en RAM": es un **presupuesto en bytes** (Chip/Fast) con
desalojo de lo no fijado (`pin`) y no referenciado (`refcount == 0`). El motor de memoria es el del
engine (`MemorySystem`/arenas); la caché solo pide y libera bloques.

## 1. AssetCache

### Modelo

Cada asset es un **slot**: id, path (o hash), tamaño, banco de memoria, prioridad, `refcount`,
último uso y estado. `pin`/prioridad alta lo hacen **no desalojable**; si falta hueco, se desaloja
un asset `Ready` con `refcount == 0`, no fijado, **de menor prioridad** y, entre iguales, el **más
viejo** (LRU). La lectura es asíncrona: la app recibe `AssetReady` (o `AssetEvicted`/`AssetError`).

```cpp
namespace eng::res {

using AssetId = eng::u16;
enum class AssetState : eng::u8 { Empty, Loading, Ready, Error };
enum class MemBank : eng::u8 { Any, Chip, Fast };

struct AssetSlot {
	const char* path = nullptr;   ///< o hash u32 en builds finales
	AssetState state = AssetState::Empty;
	MemBank bank = MemBank::Any;
	eng::u8 priority = 128;       ///< 255 = casi nunca se desaloja
	bool pinned = false;
	eng::u16 refcount = 0;
	eng::u32 last_use = 0;        ///< frame stamp
	eng::u32 size = 0;
	eng::Span<eng::u8> data {};   ///< bloque en Chip/Fast (vista tipada, no puntero crudo)
	os::FileHandle fh = 0;
};

struct CacheConfig {
	eng::u32 chip_budget = 0, fast_budget = 0; ///< bytes por banco
	eng::u16 max_assets = 64;
	bool post_ready_msg = true;
	bool post_evict_msg = false;
};

} // namespace eng::res
```

### API

```cpp
class AssetCache {
public:
	bool init(const CacheConfig& cfg);
	void shutdown();

	AssetId declare(const char* path, MemBank bank = MemBank::Any, eng::u8 prio = 128);

	void* get(AssetId id);                       ///< Ready → data; si no, lanza carga y nullptr
	void* try_get(AssetId id, AssetState* st = nullptr);

	bool prefetch(AssetId id);                   ///< carga anticipada (al entrar en zona)
	bool prefetch_many(const AssetId* ids, eng::u16 n);

	void pin(AssetId id, bool on);
	void set_priority(AssetId id, eng::u8 prio);
	void add_ref(AssetId id);
	void release(AssetId id);                    ///< refcount--; last_use = now

	void on_file_done(os::FileHandle h, eng::s32 result, const os::IoUser& user);
	void set_frame(eng::u32 frame);              ///< llamar 1× por VBlank

	eng::u32 used_chip() const, used_fast() const;
};
```

`get` devuelve `nullptr` mientras el asset está `Loading`: el juego dibuja un *placeholder* y lo
recibe por `AssetReady`. Ese es el contrato que evita bloquear el frame.

### Carga y terminación

```cpp
bool AssetCache::start_load(AssetId id) {
	AssetSlot& s = slots_[id];
	s.fh = os::file_open(s.path, os::FileMode::Read);
	if (s.fh == 0) { s.state = AssetState::Error; return false; }

	const eng::u32 sz = os::file_size(s.fh);
	if (!ensure_space(sz, s.bank, id)) { os::file_close(s.fh); s.fh = 0; return false; }
	void* mem = alloc_bytes(sz, s.bank);
	if (mem == nullptr) { os::file_close(s.fh); return false; }

	s.data = eng::Span<eng::u8> {static_cast<eng::u8*>(mem), sz};
	s.size = sz;
	s.state = AssetState::Loading;
	os::IoNotify n {};
	n.prio = os::MsgPrio::Low;
	n.user = os::IoUser { 'A', id };
	return os::file_read_async(s.fh, s.data, 0, n);
}

void AssetCache::on_file_done(os::FileHandle h, eng::s32 result, const os::IoUser& user) {
	AssetSlot& s = slots_[user.id];
	if (s.fh != h) { return; }
	os::file_close(s.fh); s.fh = 0;
	if (result < 0 || static_cast<eng::u32>(result) < s.size) {
		free_bytes(s); s.state = AssetState::Error; post_asset_msg(MsgType::AssetError, user.id);
		return;
	}
	s.state = AssetState::Ready; s.last_use = frame_;
	post_asset_msg(MsgType::AssetReady, user.id);
}
```

### Desalojo (presupuesto + prioridad + LRU)

```cpp
AssetId AssetCache::pick_victim(MemBank bank) const {
	AssetId best = 0; eng::u8 best_prio = 255; eng::u32 best_use = 0xffffffffu;
	for (AssetId i = 1; i <= count_; ++i) {
		const AssetSlot& s = slots_[i];
		if (s.state != AssetState::Ready || s.pinned || s.refcount > 0) { continue; }
		if (bank != MemBank::Any && s.bank != bank) { continue; }
		// Menor prioridad gana; a igualdad, el más viejo (LRU).
		if (s.priority < best_prio || (s.priority == best_prio && s.last_use < best_use)) {
			best = i; best_prio = s.priority; best_use = s.last_use;
		}
	}
	return best;
}
```

`ensure_space(bytes, bank, except)` desaloja víctimas hasta que quepan; con `MemBank::Any` intenta
Fast y, si no, Chip. Si no hay víctima válida, la carga falla con `AssetError` (el juego decide
subir el presupuesto, bajar la prioridad o usar *placeholders*).

### Uso por zonas

```cpp
struct ZoneAssets { AssetId tiles, enemies, sfx_amb, music; };

void on_enter_zone(ZoneAssets z) {
	cache.set_priority(z.tiles, 200);
	cache.pin(z.music, true);                  // la música de zona no se tira
	cache.prefetch_many(&z.tiles, 2);          // carga anticipada
}

void on_leave_zone(ZoneAssets z) {
	cache.pin(z.music, false);
	cache.set_priority(z.enemies, 40);         // candidatos a LRU
	cache.release(z.tiles);
	// no hace falta unload: el presupuesto los desaloja cuando haga falta
}

void game_draw() {
	if (void* t = cache.get(zone.tiles)) { blit_tiles(t); }
	else { draw_placeholder(); }               // aún Loading
}
```

Así, al moverse el protagonista de una pantalla a otra, los assets viejos y no prioritarios salen
solos cuando el presupuesto no da para los nuevos. Lo mismo para la música/SFX de una zona.

## 2. DynLoader (código relocatable)

En Amiga "a pelo" no hay `dlopen` del OS: hace falta un formato **simple y relocatable** y un
loader que lo cargue en RAM, aplique las relocaciones y exponga símbolos. Así se pueden tener
**overlays** de código (jefe de zona, minijuego) que se cargan y descargan según la zona.

### Formato `.englib`

```text
Header:
  magic "ENGL", version
  code_size, data_size, bss_size
  entry_offset            // +0 = init (se llama al cargar)
  reloc_count, export_count
Relocs:  [offset32]...    // celdas a las que sumar la dirección base (modelo abs)
Exports: [name_hash u32][offset32]...
Code · Data
```

El binario se enlaza con base 0 (o `-fPIC`) y las relocaciones las genera un script host. También
vale un Hunk Amiga (`HUNK_CODE` + `HUNK_RELOC32`) parseado por el loader.

```cpp
class DynLoader {
public:
	bool init(void* heap, eng::u32 budget, eng::u16 max_libs = 8);

	LibHandle declare(const char* path);
	bool load_async(LibHandle h);              ///< leer → relocalizar → Ready
	bool unload(LibHandle h);                  ///< solo si refcount==0 y !pinned
	void add_ref(LibHandle h);
	void release(LibHandle h);

	void* symbol(LibHandle h, const char* name);
	void* symbol(LibHandle h, eng::u32 name_hash);
	void on_file_done(os::FileHandle fh, eng::s32 result, const os::IoUser& user);
};
```

### Relocación y símbolos

```cpp
bool DynLoader::relocate(DynLib& lib) {
	auto* hdr = static_cast<EngLibHeader*>(lib.base);
	if (hdr->magic != kEngLibMagic) { return false; }
	eng::u8* code = static_cast<eng::u8*>(lib.base) + sizeof(EngLibHeader);
	const eng::u32 base = reinterpret_cast<eng::uintptr>(code);
	for (eng::u32 i = 0; i < hdr->reloc_count; ++i) {
		auto* cell = reinterpret_cast<eng::u32*>(code + hdr->relocs[i]);
		*cell += base;                          // modelo abs; o *cell += delta
	}
	lib.exports = hdr->exports();
	lib.export_count = hdr->export_count;
	lib.entry = reinterpret_cast<LibFn>(code + hdr->entry_offset);
	if (lib.entry != nullptr) { lib.entry(); }  // init
	lib.state = LibState::Ready;
	return true;
}
```

### Uso

```cpp
auto boss = dyn.declare("libs/boss.englib");
dyn.load_async(boss);

// en LibLoaded:
using BossUpdate = void (*)(Game&);
auto fn = reinterpret_cast<BossUpdate>(dyn.symbol(boss, "boss_update"));
if (fn != nullptr) { fn(game); }

// al salir de la zona:
dyn.release(boss);
dyn.unload(boss);                               // libera RAM para otra lib
```

**Seguridad en 68000:** no hay NX; el código debe estar en un rango ejecutable (mismo *address
space*). Preferir **Fast RAM** para código si hay expansión; en A500 stock todo es Chip.

## 3. Mensajes

La E/S y los recursos van en `MsgPrio::Low` (no compiten con la entrada). Sobre `FileDone`/
`FileError` se añaden los avisos de recurso, con el `IoUser` discriminando el consumidor:

```cpp
enum class MsgType : eng::u8 {
	// …
	FileDone, FileError,
	AssetReady, AssetEvicted, AssetError,
	LibLoaded, LibError, LibUnloaded,
};
```

## 4. Fachada `Resources`

`eng::res::Resources` reúne caché y loader y **enruta** los mensajes de E/S por el `tag`:

```cpp
class Resources {
public:
	AssetCache cache;
	DynLoader libs;

	void on_msg(const os::Msg& m) {
		switch (m.type) {
		case MsgType::FileDone:
		case MsgType::FileError:
			if (m.file.user.tag == 'A') { cache.on_file_done(m.file.handle, m.file.result, m.file.user); }
			else if (m.file.user.tag == 'L') { libs.on_file_done(m.file.handle, m.file.result, m.file.user); }
			break;
		case MsgType::AssetReady:  on_asset_ready(static_cast<AssetId>(m.file.user.id)); break;
		default: break;
		}
	}
	void begin_frame(eng::u32 frame) { cache.set_frame(frame); }
};
```

## 5. Flujo completo (cambio de zona)

```text
Jugador entra en zona B
  → prefetch tiles_B, sfx_B (prio 180)
  → load_async(lib_enemies_B)
  → pin(music_B)

FileDone tiles_B → AssetReady → la zona puede dibujar
FileDone lib     → relocate → LibLoaded → enemy_update = symbol(...)

Poco Chip libre + entra zona C
  → prefetch tiles_C
  → ensure_space: desaloja enemies_A (ref=0, prio baja, LRU)
  → AssetEvicted (opcional)

Sale zona B
  → release/unpin music_B, unload lib_enemies_B
```

## 6. Resumen

| Pieza | Función |
|---|---|
| VFS ([`MINI_OS_IO.md`](MINI_OS_IO.md)) | create/open/read/write/delete + `FileDone` |
| `AssetCache` | declare/prefetch/get, pin, prioridad, LRU, presupuestos Chip/Fast |
| `DynLoader` | `.englib` con relocs y exports, load/unload async |
| Mensajes | `AssetReady`/`Evicted`/`Error`, `LibLoaded`/`Error`/`Unloaded` sobre `FileDone` |
| Juego | prefetch por zona; `get` devuelve `nullptr` mientras `Loading` |

No hace falta un SO de archivos complejo: hace falta **E/S asíncrona + política de memoria + un
formato de código relocatable**. La caché garantiza que los assets viejos y no prioritarios salen
solos cuando el presupuesto no da para los nuevos.

## 7. Referencias

- [`MINI_OS_IO.md`](MINI_OS_IO.md) — VFS y E/S asíncrona (`FileDone`/`FileError`, `IoUser`).
- [`SCENE_AND_RESOURCES.md`](SCENE_AND_RESOURCES.md) — escena retenida y modelo de ocupación.
- [`STREAMING_LOADER.md`](STREAMING_LOADER.md) — loader de chunks y `ChunkCache`.
- [`MEMORY_MODEL.md`](MEMORY_MODEL.md) — arenas Chip/Slow/Frame del perfil A500.
- Plan: [`ROADMAP_RESOURCES.md`](../../guides/roadmap/ROADMAP_RESOURCES.md).
