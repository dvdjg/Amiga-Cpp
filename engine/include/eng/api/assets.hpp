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
#include <eng/graphics/sprite_asset.hpp>
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

	/// Registra `name` **copiando el blob a Chip**. El `Tag` fija el dominio (alineación y vista):
	/// `PlaneTag` (bitmap), `MusicTag` (módulo), `SpriteTag` (hoja), `PaletteTag` (paleta). `false`
	/// si no cabe en la arena.
	template <class Tag>
	bool add(const char* name, const eng::u8* data, eng::usize size) noexcept {
		if (!m_mem.valid()) {
			return false;
		}
		const auto block = eng::res::load<Tag>(*m_mem.get(), eng::Span<const eng::u8> {data, size});
		if (!block.valid()) {
			return false;
		}
		if (!track(block)) { // dueño: se libera en `reset_phase`
			return false;
		}
		return m_table.template add<Tag>(name, block.view.as_const().data(), size);
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
	/// reserva. `reset_phase` no toca el `AssetTable` (los nombres quedan, las vistas apuntan a
	/// memoria liberada): llamar al terminar la fase, no durante el render.
	void reset_phase() noexcept {
		if (!m_mem.valid()) {
			return;
		}
		while (m_tracked_count > 0u) {
			--m_tracked_count;
			release_tracked(m_tracked[m_tracked_count].ptr);
		}
	}
	void clear() noexcept {
		reset_phase();
		m_table = eng::res::AssetTable {};
	}

	[[nodiscard]] eng::u8 tracked_count() const noexcept { return m_tracked_count; }

	/// **Módulo de música** por nombre. El formato y el buffer de descompresión los resuelve el
	/// engine al reproducirlo (`app.audio().play_music(assets.music("n"))`).
	[[nodiscard]] eng::audio::MusicModule music(const char* name) const noexcept {
		return eng::audio::MusicModule {m_table.template get<eng::MusicTag>(name).raw()};
	}

	/// **Hoja de sprites** por nombre + su geometría (`Bob` sin hoja) → `Sprite` para
	/// `screen.sprite(...)`.
	[[nodiscard]] eng::graphics::Sprite sprite(const char* name,
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
	[[nodiscard]] eng::Span<const eng::u8> bytes(const char* name) const noexcept {
		return m_table.template get<eng::PlaneTag>(name).raw();
	}

	/// **Paleta** (palabras COLOR) por nombre. Los blobs se registran como bytes; aquí se releen
	/// como palabras (frontera bytes→`u16`, como el `PaletteWords` de la escena).
	[[nodiscard]] eng::Span<const eng::u16> palette(const char* name) const noexcept {
		const ByteView<eng::PaletteTag> v = m_table.template get<eng::PaletteTag>(name);
		return eng::Span<const eng::u16> {
			reinterpret_cast<const eng::u16*>(v.data()), v.size() / 2u};
	}

	[[nodiscard]] bool has(const char* name) const noexcept { return m_table.has(name); }

private:
	/// Rastrea el bloque (por su puntero y el banco de origen) para poder liberarlo después.
	template <class Tag>
	bool track(const Block<Tag>& block) noexcept {
		if (m_tracked_count >= kMaxBlocks) {
			return false;
		}
		m_tracked[m_tracked_count++] = Tracked {block.view.data(), block.kind};
		return true;
	}
	/// Libera un bloque rastreado por su puntero, eligiendo el banco según el `kind` guardado.
	void release_tracked(const eng::u8* ptr) noexcept {
		for (eng::u8 i = 0u; i < m_tracked_count; ++i) {
			if (m_tracked[i].ptr == ptr) {
				const MemoryKind k = m_tracked[i].kind;
				if (k == MemoryKind::Chip) {
					m_mem.get()->chip().release(ptr);
				} else if (k == MemoryKind::Fast) {
					m_mem.get()->fast().release(ptr);
				} else {
					m_mem.get()->slow().release(ptr);
				}
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

	eng::Ref<MemoryManager> m_mem {};
	res::AssetTable m_table {};
	Tracked m_tracked[kMaxBlocks] {};
	eng::u8 m_tracked_count = 0u;
};

} // namespace eng
