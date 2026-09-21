#pragma once

/// \file dynloader.hpp
/// **Loader de código relocatable** (`eng::res`): carga una imagen **`.englib`** en RAM, aplica las
/// **relocaciones** y expone **símbolos** por hash. Permite overlays de código (jefe de zona,
/// minijuego) cargables/descargables. Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` §2.
///
/// El loader **no posee** la imagen (la reserva el llamador, p. ej. desde la `AssetCache`); aquí
/// está el parseo + relocación + búsqueda de símbolos, **puro** y host-testable.
///
/// ```text
///   Header (24 B): magic 'ENGL', version, code_size, data_size, bss_size,
///                  entry_offset, reloc_count, export_count
///   Code (code_size B, multiplo de 4)
///   Relocs:  reloc_count x u32 (offset dentro del code)
///   Exports: export_count x { u32 name_hash; u32 offset }
/// ```

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::res {

using LibHandle = eng::u16; ///< 0 = inválido

/// Estado de una lib.
enum class LibState : eng::u8 { Empty = 0, Ready, Error };

/// Entrada de export.
struct LibExport {
	eng::u32 name_hash;
	eng::u32 offset; ///< desde el inicio del code
};

/// Cabecera `.englib` (24 B, sin padding).
struct EngLibHeader {
	eng::u32 magic;
	eng::u16 version;
	eng::u16 code_size;
	eng::u32 data_size;
	eng::u32 bss_size;
	eng::u32 entry_offset;
	eng::u16 reloc_count;
	eng::u16 export_count;
};

inline constexpr eng::u32 kEngLibMagic = 0x454e474cu; // 'ENGL'

/// **Loader de código relocatable** (capacidad fija, sin heap).
class DynLoader {
public:
	static constexpr eng::u16 kMaxLibs = 8u;

	/// Registra una lib (el `path` es informativo). Devuelve el handle (1..N) o 0 si no cabe.
	[[nodiscard]] LibHandle declare(const char* path) noexcept {
		if (m_count >= kMaxLibs) {
			return 0u;
		}
		Lib& l = m_libs[m_count];
		l = Lib {};
		l.path = path;
		++m_count;
		return static_cast<LibHandle>(m_count);
	}

	/// Carga y relocaliza la imagen `.englib` (no la posee). `false` si el formato no es válido.
	bool load(LibHandle h, eng::Span<eng::u8> image) noexcept {
		if (!valid(h)) {
			return false;
		}
		Lib& l = m_libs[h - 1u];
		if (image.size() < sizeof(EngLibHeader)) {
			l.state = LibState::Error;
			return false;
		}
		auto* hdr = reinterpret_cast<EngLibHeader*>(image.data());
		if (hdr->magic != kEngLibMagic || (hdr->code_size % 4u) != 0u) {
			l.state = LibState::Error;
			return false;
		}
		eng::u8* const code = image.data() + sizeof(EngLibHeader);
		const eng::u32 base = static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(code));

		// Relocaciones: sumar la direccion base a cada celda.
		auto* relocs = reinterpret_cast<eng::u32*>(code + hdr->code_size);
		for (eng::u16 i = 0u; i < hdr->reloc_count; ++i) {
			auto* cell = reinterpret_cast<eng::u32*>(code + relocs[i]);
			*cell = *cell + base;
		}

		l.base = code;
		l.size = hdr->code_size;
		l.exports = reinterpret_cast<LibExport*>(relocs + hdr->reloc_count);
		l.export_count = hdr->export_count;
		l.state = LibState::Ready;
		return true;
	}

	/// Dirección del símbolo `name_hash` en la lib `h` (o `nullptr`).
	[[nodiscard]] void* symbol(LibHandle h, eng::u32 name_hash) const noexcept {
		if (!valid(h)) {
			return nullptr;
		}
		const Lib& l = m_libs[h - 1u];
		if (l.state != LibState::Ready) {
			return nullptr;
		}
		for (eng::u16 i = 0u; i < l.export_count; ++i) {
			if (l.exports[i].name_hash == name_hash) {
				return l.base + l.exports[i].offset;
			}
		}
		return nullptr;
	}

	/// Igual que `symbol(h, name_hash)` con el nombre en claro.
	[[nodiscard]] void* symbol(LibHandle h, const char* name) const noexcept {
		return symbol(h, hash_name(name));
	}

	/// Descarga la lib (el llamador libera la imagen).
	void unload(LibHandle h) noexcept {
		if (valid(h)) {
			m_libs[h - 1u] = Lib {};
		}
	}

	/// Estado de la lib (`Empty` si el handle no es valido).
	[[nodiscard]] LibState state(LibHandle h) const noexcept {
		return valid(h) ? m_libs[h - 1u].state : LibState::Empty;
	}
	[[nodiscard]] eng::u16 count() const noexcept { return m_count; }

	/// Hash FNV-1a de 32 bits del nombre de un símbolo.
	[[nodiscard]] static constexpr eng::u32 hash_name(const char* s) noexcept {
		eng::u32 h = 2166136261u;
		while (*s != '\0') {
			h = (h ^ static_cast<eng::u8>(*s)) * 16777619u;
			++s;
		}
		return h;
	}

private:
	struct Lib {
		const char* path = nullptr;
		LibState state = LibState::Empty;
		eng::u8* base = nullptr;
		eng::u32 size = 0u;
		LibExport* exports = nullptr;
		eng::u16 export_count = 0u;
	};

	[[nodiscard]] bool valid(LibHandle h) const noexcept { return h >= 1u && h <= m_count; }

	Lib m_libs[kMaxLibs] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::res
