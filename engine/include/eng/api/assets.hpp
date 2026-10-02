#pragma once

/// \file assets.hpp
/// **Assets de juego** (`eng::Assets`): tabla **por nombre** + accesores de **dominio**. Al registrar
/// un blob lo **copia a Chip** (DMA: música/samples, sprites, bitmap) igual que `res::load`, de modo
/// que el juego no ve `Block<Tag>`, bytes crudos ni geometría de audio. Complementa a
/// `eng::res::AssetTable` (bajo nivel) y `eng::res::AssetRuntime` (ficheros/E/S). Ver
/// `docs/guides/roadmap/ROADMAP_GAME_API.md` §4.
///
/// ```cpp
/// INCBIN(abyss_mod, "assets/amiga/audio/testmod.p61");
/// eng::Assets assets {app.memory_manager()};
/// assets.add<eng::MusicTag>("mod", incbin_abyss_mod_start, INCBIN_SIZE(abyss_mod));
/// app.audio().play_music(assets.music("mod"));   // el engine detecta el formato y el buffer
/// ```
///
/// **Nota de capas**: incluye `eng/audio/music_player.hpp` (para `MusicModule`) que trae asm de
/// Amiga; por eso **no** se incluye desde `eng/api/api.hpp` (que es host-compilable). Las demos
/// Amiga lo incluyen directamente.

#include <eng/audio/music_player.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/graphics/bitmap_view.hpp>
#include <eng/graphics/plane_layout.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/res/asset_table.hpp>
#include <eng/res/load.hpp>

namespace eng {

/// Assets del juego por **nombre**: registra blobs (de `INCBIN`/memoria) copiándolos a Chip, y los
/// entrega ya como objetos de dominio (`MusicModule`, `Sprite`, bytes, paleta).
///
/// Es la **puerta única de reserva** de la fachada (Fase 1 de `ROADMAP_MEMORY_OWNERSHIP.md`): el
/// dominio (`DomainAsset<Tag>`) elige el banco (Chip para DMA, Fast→Slow para CPU) y el `Assets`
/// conserva el `Block` como dueño, entregando **vistas no propietarias**. `reset_phase()` libera
/// todos los bloques en **orden inverso** al de reserva.
class Assets {
public:
	static constexpr eng::u8 kMaxBlocks = 16u;

	Assets() = default;
	Assets(const Assets&) = delete;
	Assets& operator=(const Assets&) = delete;

	/// Liga el gestor de memoria donde se copian los blobs. Llamar antes de `add`/`create`.
	void bind(MemoryManager& memory) noexcept { m_mem = memory; }

	/// Registra un bitmap planar con nombre y geometría de origen junto a su almacenamiento Chip.
	/// La metainformación se conserva en la tabla fija de Assets y no se vuelve a pasar al dibujar.
	[[nodiscard]] bool add_bitmap(eng::util::StringView name, const eng::u8* data, eng::usize size,
				      eng::u16 width, eng::u16 height, eng::u8 planes,
				      eng::graphics::PlaneLayout layout) noexcept {
		if (name.empty() || data == nullptr || width == 0u || height == 0u || planes == 0u ||
		    planes > graphics::kBlitterMaxPlanes ||
		    width / graphics::kPixelsPerBlitterWord > graphics::kBlitterMaxWordsPerRow ||
		    height > graphics::kBlitterMaxRows ||
		    (width & (graphics::kPixelsPerBlitterWord - 1u)) != 0u ||
		    m_bitmap_count >= kMaxBlocks || m_table.has(name)) {
			return false;
		}
		const eng::u32 row_bytes = static_cast<eng::u32>(width / graphics::kPixelsPerByte);
		const eng::u32 expected = row_bytes * height * planes;
		if (size != expected || !add<eng::PlaneTag>(name, data, size)) return false;
		m_bitmaps[m_bitmap_count++] = BitmapAsset {name, width, height, planes, layout};
		return true;
	}

	/// Bitmap por nombre con geometría registrada en `add_bitmap`; vacío si no existe.
	[[nodiscard]] eng::graphics::ChipBitmapView<eng::PlaneTag> bitmap(
		eng::util::StringView name) const noexcept {
		for (eng::u8 i = 0u; i < m_bitmap_count; ++i) {
			const BitmapAsset& asset = m_bitmaps[i];
			if (asset.name.size() != name.size()) continue;
			eng::usize j = 0u;
			for (; j < name.size() && asset.name[j] == name[j]; ++j) {}
			if (j != name.size()) continue;
			const auto bytes = m_table.template get<eng::PlaneTag>(name);
			eng::graphics::ChipBitmapView<eng::PlaneTag> view {};
			if (bytes.empty()) return view;
			view.planes = eng::as_chip(bytes);
			view.width = asset.width;
			view.height = asset.height;
			view.row_bytes = static_cast<eng::u16>(asset.width / graphics::kPixelsPerByte);
			view.plane_count = asset.planes;
			view.layout = asset.layout;
			return view;
		}
		return {};
	}

	/// Registra `name` **copiando el blob a Chip**. El `Tag` fija el dominio (alineación y vista):
	/// `PlaneTag` (bitmap), `MusicTag` (módulo), `SpriteTag` (hoja), `PaletteTag` (paleta). `false`
	/// si no cabe o no hay gestor. Atajo de `add_checked` (sin la causa).
	template <class Tag>
	bool add(const char* name, const eng::u8* data, eng::usize size) noexcept {
		return name != nullptr && add<Tag>(eng::util::StringView {name}, data, size);
	}
	template <class Tag>
	bool add(eng::util::StringView name, const eng::u8* data, eng::usize size) noexcept {
		return add_checked<Tag>(name, data, size).has_value();
	}

	/// Como `add`, devolviendo la **causa del fallo** (`Result`): `InvalidArgument` (sin gestor,
	/// nombre/datos nulos), `OutOfMemory` (no cabe) o `HardwareLimit` (tabla llena).
	template <class Tag>
	[[nodiscard]] eng::util::Expected<void, eng::Result>
	add_checked(const char* name, const eng::u8* data, eng::usize size) noexcept {
		if (name == nullptr) return eng::util::unexpected(eng::Result::InvalidArgument);
		return add_checked<Tag>(eng::util::StringView {name}, data, size);
	}
	template <class Tag>
	[[nodiscard]] eng::util::Expected<void, eng::Result>
	add_checked(eng::util::StringView name, const eng::u8* data, eng::usize size) noexcept {
		if (!m_mem.valid() || name.empty() || data == nullptr) {
			return eng::util::unexpected(eng::Result::InvalidArgument);
		}
		const auto block = eng::res::load<Tag>(*m_mem.get(), eng::Span<const eng::u8> {data, size});
		if (!block.valid()) {
			return eng::util::unexpected(eng::Result::OutOfMemory);
		}
		// Rastrea primero (dueño) y registra después: si la tabla se llena, deshace el track y
		// libera el bloque, de modo que **no queda** ni memoria viva ni asset sin dueño.
		if (!track(block)) {
			release_to_bank(block.view.data(), res::DomainAsset<Tag>::kind);
			return eng::util::unexpected(eng::Result::HardwareLimit);
		}
		if (!m_table.template add<Tag>(name,
						       eng::Span<const eng::u8> {block.view.as_const().data(), size})) {
			release_tracked(block.view.data());
			return eng::util::unexpected(eng::Result::HardwareLimit);
		}
		return {};
	}

	/// **Reserva** `bytes` para un recurso del dominio `Tag` (sin copia). El banco lo elige el
	/// dominio (Chip para DMA, Fast→Slow para CPU). Devuelve un `Block<Tag>` **dueño** (el
	/// llamador lo usa y puede `release`); con `Bank=Chip` el bloque da `Address<Chip>` para DMA.
	template <class Tag>
	[[nodiscard]] Block<Tag> create(u32 bytes) noexcept {
		if (!m_mem.valid()) {
			return {};
		}
		Block<Tag> block = (res::DomainAsset<Tag>::kind == MemoryKind::Chip)
					   ? Block<Tag> {m_mem.get()->chip().reserve<Tag>(
								 bytes, res::DomainAsset<Tag>::align)}
					   : eng::fast_or_slow<Tag>(*m_mem.get(), bytes, res::DomainAsset<Tag>::align);
		return block;
	}

	/// Como `create`, devolviendo el bloque **o la causa** (`InvalidArgument` sin gestor,
	/// `OutOfMemory` si no cabe).
	template <class Tag>
	[[nodiscard]] eng::util::Expected<Block<Tag>, eng::Result> create_checked(u32 bytes) noexcept {
		if (!m_mem.valid()) {
			return eng::util::unexpected(eng::Result::InvalidArgument);
		}
		Block<Tag> block = create<Tag>(bytes);
		if (!block.valid()) {
			return eng::util::unexpected(eng::Result::OutOfMemory);
		}
		return block;
	}

	/// **Devuelve** un bloque reservado con `create` al banco correspondiente (según su `kind`).
	/// La liberación ordenada (sin DMA pendiente) es del flujo; ver `MEMORY_OWNERSHIP.md`.
	template <class Tag>
	void release(const Block<Tag>& block) noexcept {
		if (!m_mem.valid() || !block.valid()) {
			return;
		}
		release_tracked(block.view.data());
	}

	/// **Libera todos** los bloques rastreados (los de `add` y `create`) en orden inverso al de
	/// reserva e invalida la tabla declarativa; registrar de nuevo los assets para la siguiente fase.
	void reset_phase() noexcept {
		if (!m_mem.valid()) {
			return;
		}
		// Orden inverso al de reserva (LIFO de fases). Libera por el puntero+banco guardados y
		// vacía el registro entero: no usar `release_tracked` (que reordena y decrementa).
		while (m_tracked_count > 0u) {
			--m_tracked_count;
			release_to_bank(m_tracked[m_tracked_count].ptr, m_tracked[m_tracked_count].kind);
		}
		m_table = eng::res::AssetTable {};
		m_bitmap_count = 0u;
	}
	void clear() noexcept {
		reset_phase();
		m_table = eng::res::AssetTable {};
		m_bitmap_count = 0u;
	}

	[[nodiscard]] eng::u8 tracked_count() const noexcept { return m_tracked_count; }
	/// Nº de assets registrados en la tabla (por nombre).
	[[nodiscard]] eng::u8 count() const noexcept { return m_table.count(); }
	[[nodiscard]] bool valid_view(eng::util::StringView name,
				       eng::Span<const eng::u8> data) const noexcept {
		return m_table.valid_view(name, data);
	}

	/// **Módulo de música** por nombre. El formato y el buffer de descompresión los resuelve el
	/// engine al reproducirlo (`app.audio().play_music(assets.music("n"))`).
	[[nodiscard]] eng::audio::MusicModule music(eng::util::StringView name) const noexcept {
		return eng::audio::MusicModule {m_table.template get<eng::MusicTag>(name).raw()};
	}

	/// **Hoja de sprites** por nombre + su geometría (`Bob` sin hoja) → `Sprite` para
	/// `screen.sprite(...)`.
	[[nodiscard]] eng::graphics::Sprite sprite(eng::util::StringView name,
						   const eng::graphics::Bob& desc) const noexcept {
		const ByteView<eng::SpriteTag> v = m_table.template get<eng::SpriteTag>(name);
		eng::graphics::Bob bob = desc;
		// El asset vive en Chip (procedencia garantizada por el loader): puente explícito a la
		// vista con banco Chip (el Blitter solo lee Chip).
		bob.sheet = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(v.data()), v.size()};
		return eng::graphics::Sprite {bob, static_cast<eng::u32>(v.size())};
	}

	/// **Bytes** de un asset (p. ej. el bitmap de fondo) por nombre.
	[[nodiscard]] eng::Span<const eng::u8> bytes(eng::util::StringView name) const noexcept {
		return m_table.template get<eng::PlaneTag>(name).raw();
	}

	/// **Paleta** (palabras COLOR) por nombre. Los blobs se registran como bytes; aquí se releen
	/// como palabras (frontera bytes→`u16`, como el `PaletteWords` de la escena).
	[[nodiscard]] eng::Span<const eng::u16> palette(eng::util::StringView name) const noexcept {
		const ByteView<eng::PaletteTag> v = m_table.template get<eng::PaletteTag>(name);
		return eng::Span<const eng::u16> {
			reinterpret_cast<const eng::u16*>(v.data()), v.size() / 2u};
	}

	[[nodiscard]] bool has(eng::util::StringView name) const noexcept { return m_table.has(name); }

private:
	/// Rastrea el bloque (por su puntero y el **banco del dominio**) para liberarlo después. El
	/// banco lo fija `DomainAsset<Tag>` (no `block.kind`, que es `Any` en un `Block<Tag>` genérico).
	template <class Tag>
	bool track(const Block<Tag>& block) noexcept {
		if (m_tracked_count >= kMaxBlocks) {
			return false;
		}
		m_tracked[m_tracked_count++] = Tracked {block.view.data(), res::DomainAsset<Tag>::kind};
		return true;
	}
	/// Devuelve un bloque al banco que le corresponde según `kind` (por puntero).
	void release_to_bank(const eng::u8* ptr, MemoryKind kind) noexcept {
		if (kind == MemoryKind::Chip) {
			m_mem.get()->chip().release(ptr);
		} else if (kind == MemoryKind::Fast) {
			m_mem.get()->fast().release(ptr);
		} else {
			m_mem.get()->slow().release(ptr);
		}
	}
	/// Libera un bloque rastreado por su puntero, eligiendo el banco según el `kind` guardado.
	void release_tracked(const eng::u8* ptr) noexcept {
		for (eng::u8 i = 0u; i < m_tracked_count; ++i) {
			if (m_tracked[i].ptr == ptr) {
				const MemoryKind k = m_tracked[i].kind;
				release_to_bank(ptr, k);
				for (eng::u8 j = i; j + 1u < m_tracked_count; ++j) {
					m_tracked[j] = m_tracked[j + 1u];
				}
				--m_tracked_count;
				return;
			}
		}
	}

	struct Tracked {
		const eng::u8* ptr = nullptr;
		MemoryKind kind = MemoryKind::Chip;
	};
	struct BitmapAsset {
		eng::util::StringView name {};
		eng::u16 width = 0u;
		eng::u16 height = 0u;
		eng::u8 planes = 0u;
		eng::graphics::PlaneLayout layout = eng::graphics::PlaneLayout::Interleaved;
	};

	eng::Ref<MemoryManager> m_mem {};
	res::AssetTable m_table {};
	Tracked m_tracked[kMaxBlocks] {};
	eng::u8 m_tracked_count = 0u;
	BitmapAsset m_bitmaps[kMaxBlocks] {};
	eng::u8 m_bitmap_count = 0u;
};

} // namespace eng
