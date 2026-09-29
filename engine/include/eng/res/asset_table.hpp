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
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>

namespace eng::res {

/// Tabla de capacidad fija (sin heap) de assets **en memoria**, por nombre y dominio.
class AssetTable {
public:
	static constexpr eng::u8 kMaxAssets = 16u;

	/// Registra `name` con `size` bytes. `false` si no cabe (o nombre/datos nulos).
	template <class Tag>
	bool add(const char* name, const eng::u8* data, eng::usize size) noexcept {
		if (name == nullptr || data == nullptr || m_count >= kMaxAssets) {
			return false;
		}
		m_entries[m_count++] = Entry {name, data, static_cast<eng::u32>(size)};
		return true;
	}

	/// Vista de dominio del asset `name`; vacía si no existe.
	template <class Tag>
	[[nodiscard]] ByteView<Tag> get(const char* name) const noexcept {
		const eng::u8 i = find(name);
		return i != kNotFound ? ByteView<Tag>(m_entries[i].data, m_entries[i].size)
				      : ByteView<Tag> {};
	}

	/// `true` si el asset `name` está registrado.
	[[nodiscard]] bool has(const char* name) const noexcept { return find(name) != kNotFound; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }

private:
	static constexpr eng::u8 kNotFound = 0xffu;

	struct Entry {
		const char* name = nullptr;
		const eng::u8* data = nullptr;
		eng::u32 size = 0u;
	};

	/// ¿Dos cadenas C son iguales? (sin libc)
	[[nodiscard]] static bool name_eq(const char* a, const char* b) noexcept {
		while (*a != '\0' && *a == *b) {
			++a;
			++b;
		}
		return *a == *b;
	}

	/// Índice del asset `name` (`kNotFound` si no está). Devuelve índice (no puntero).
	[[nodiscard]] eng::u8 find(const char* name) const noexcept {
		if (name == nullptr) {
			return kNotFound;
		}
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (name_eq(m_entries[i].name, name)) {
				return i;
			}
		}
		return kNotFound;
	}

	Entry m_entries[kMaxAssets] {};
	eng::u8 m_count = 0u;
};

} // namespace eng::res
