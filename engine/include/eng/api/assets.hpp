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
class Assets {
public:
	Assets() = default;
	Assets(const Assets&) = delete;
	Assets& operator=(const Assets&) = delete;

	/// Liga el gestor de memoria donde se copian los blobs. Llamar antes de `add`.
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
		return m_table.template add<Tag>(name, block.view.as_const().data(), size);
	}

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
		bob.sheet = v.data();
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
	eng::Ref<MemoryManager> m_mem {};
	res::AssetTable m_table {};
};

} // namespace eng
