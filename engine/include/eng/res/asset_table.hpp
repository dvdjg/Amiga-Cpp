#pragma once

/// \file asset_table.hpp
/// **Tabla declarativa de assets en memoria** (`eng::res::AssetTable`): el juego registra sus
/// blobs (incrustados con `INCBIN`/`INCBIN_CHIP`) por **nombre** y los pide con su **dominio**
/// (`table.get<PlaneTag>("abyss")` -> `ByteView<PlaneTag>`), sin `__asm__`, tamaños ni punteros
/// en la lógica. Para assets de fichero (E/S asíncrona) está `eng::res::AssetRuntime`.
///
/// ```cpp
/// INCBIN_CHIP(abyss_img, "assets/amiga/sprites/abyss/abyss.bpl");
/// eng::res::AssetTable assets {};
/// assets.add<eng::PlaneTag>("abyss", incbin_abyss_img_start, bytes);
/// auto img = assets.get<eng::PlaneTag>("abyss");   // ByteView<PlaneTag>
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/string_view.hpp>

namespace eng::res {

/// Tabla de capacidad fija (sin heap) de assets **en memoria**, por nombre y dominio.
class AssetTable {
public:
	static constexpr eng::u8 kMaxAssets = 16u;

	/// Registra vistas no propietarias de nombre y bytes; quien llama mantiene vivos ambos rangos.
	template <class Tag>
	bool add(eng::util::StringView name, eng::Span<const eng::u8> data) noexcept {
		if (name.empty() || data.empty() || m_count >= kMaxAssets || find(name) != kNotFound) {
			return false;
		}
		m_entries[m_count++] = Entry {name, data};
		return true;
	}

	/// Vista de dominio del asset `name`; vacía si no existe.
	template <class Tag>
	[[nodiscard]] ByteView<Tag> get(eng::util::StringView name) const noexcept {
		const eng::u8 i = find(name);
		return i != kNotFound ? ByteView<Tag>(m_entries[i].data.data(), m_entries[i].data.size())
				      : ByteView<Tag> {};
	}
	/// `true` si la vista sigue señalando los mismos bytes del registro; sirve para auditar tablas
	/// que almacenan referencias externas sin convertirlas en propietarias.
	[[nodiscard]] bool valid_view(eng::util::StringView name,
				      eng::Span<const eng::u8> data) const noexcept {
		const eng::u8 i = find(name);
		return i != kNotFound && m_entries[i].data.data() == data.data() &&
			m_entries[i].data.size() == data.size();
	}

	/// `true` si el asset `name` está registrado.
	[[nodiscard]] bool has(eng::util::StringView name) const noexcept { return find(name) != kNotFound; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }

private:
	static constexpr eng::u8 kNotFound = 0xffu;

	struct Entry {
		eng::util::StringView name {};
		eng::Span<const eng::u8> data {};
	};

	/// Índice del asset `name` (`kNotFound` si no está). Devuelve índice (no puntero).
	[[nodiscard]] eng::u8 find(eng::util::StringView name) const noexcept {
		for (eng::u8 i = 0u; i < m_count; ++i) {
			const eng::util::StringView candidate = m_entries[i].name;
			if (candidate.size() != name.size()) continue;
			eng::usize j = 0u;
			for (; j < name.size() && candidate[j] == name[j]; ++j) {}
			if (j == name.size()) {
				return i;
			}
		}
		return kNotFound;
	}

	Entry m_entries[kMaxAssets] {};
	eng::u8 m_count = 0u;
};

} // namespace eng::res
