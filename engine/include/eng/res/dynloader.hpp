#pragma once

/// \file dynloader.hpp
/// **Loader de código relocatable** (`eng::res`): carga un módulo dinámico en RAM, aplica las
/// **relocaciones** y expone **símbolos** por hash. Permite overlays de código (jefe de zona,
/// minijuego) cargables/descargables. Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` §2.
///
/// Soporta **dos formatos**, detectados por el primer longword:
/// - **`.englib`** (magic `'ENGL'`): contenedor propio compacto, en orden nativo de la máquina.
///   El código vive **in situ** en la imagen (la imagen debe permanecer válida).
/// - **HUNK** (magic `HUNK_HEADER`): el formato nativo de AmigaOS (`hunk.hpp`). Los segmentos se
///   copian a una `eng::LinearArena` del llamador, así que la imagen puede liberarse tras cargar.
///
/// El loader **no posee** la memoria (la reserva el llamador, p. ej. desde la `AssetCache`);
/// aquí está el parseo + relocación + búsqueda de símbolos, **puro** y host-testable.
///
/// ```text
///   .englib  Header (24 B): magic 'ENGL', version, code_size, data_size, bss_size,
///                          entry_offset, reloc_count, export_count
///            Code (code_size B, multiplo de 4) · Relocs · Exports { name_hash, offset }
///   HUNK     ver hunk.hpp (HUNK_HEADER + CODE/DATA/BSS + RELOC/SYMBOL + HUNK_END)
/// ```

#include <eng/core/data/byte_order.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/arena.hpp>
#include <eng/res/hunk.hpp>
#include <eng/res/symbol_hash.hpp>

namespace eng::res {

using LibHandle = eng::u16; ///< 0 = inválido

/// Estado de una lib.
enum class LibState : eng::u8 { Empty = 0, Ready, Error };

/// Formato del módulo cargado.
enum class LibFormat : eng::u8 { None = 0, EngLib, Hunk };

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

/// **Loader de código relocatable** (capacidad fija, sin heap). Acepta `.englib` y HUNK.
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

	/// Carga y relocaliza la imagen (no la posee). Detecta el formato por el primer longword:
	/// **`.englib`** (relocalización **in situ**, `pool` no se usa) o **HUNK** (se copia a
	/// `pool`, que entonces es obligatorio). `false` si el formato no es válido o no cabe.
	bool load(LibHandle h, eng::Span<eng::u8> image, eng::LinearArena* pool = nullptr) noexcept {
		if (!valid(h)) {
			return false;
		}
		Lib& l = m_libs[h - 1u];
		if (image.size() < 4u) {
			l.state = LibState::Error;
			return false;
		}
		const eng::u32 be = eng::read_be32(image.data());
		if (be == kHunkHeaderMagic) {
			if (pool == nullptr || !l.hunk.load(image, *pool)) {
				l.state = LibState::Error;
				return false;
			}
			l.fmt = LibFormat::Hunk;
			l.state = LibState::Ready;
			return true;
		}
		if (be != kEngLibMagic && eng::read_le32(image.data()) != kEngLibMagic) {
			l.state = LibState::Error;
			return false;
		}
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
		l.fmt = LibFormat::EngLib;
		l.state = LibState::Ready;
		return true;
	}

	/// Carga **poseyendo la memoria** en `mem` (banco por segmento, R6.3/R6.6): HUNK usa
	/// `HunkImage::load(image, mem, policy)`; `.englib` **copia** la imagen a un bloque y relocaliza
	/// allí. `unload(h, mem)` libera. `false` (sin dejar memoria) si el formato no es válido o no cabe.
	bool load(LibHandle h, eng::Span<eng::u8> image, MemoryManager& mem,
		  MemoryPolicy policy = kHunkAnyPolicy) noexcept {
		if (!valid(h)) {
			return false;
		}
		Lib& l = m_libs[h - 1u];
		if (image.size() < 4u) {
			l.state = LibState::Error;
			return false;
		}
		if (eng::read_be32(image.data()) == kHunkHeaderMagic) {
			if (!l.hunk.load(image, mem, policy)) {
				l.state = LibState::Error;
				return false;
			}
			l.owns_hunk = true;
			l.fmt = LibFormat::Hunk;
			l.state = LibState::Ready;
			return true;
		}
		// `.englib`: copiar la imagen a un bloque y relocalizar sobre la COPIA.
		if (image.size() < sizeof(EngLibHeader)) {
			l.state = LibState::Error;
			return false;
		}
		const EngLibHeader& hdr0 = *reinterpret_cast<const EngLibHeader*>(image.data());
		if (hdr0.magic != kEngLibMagic || (hdr0.code_size % 4u) != 0u) {
			l.state = LibState::Error;
			return false;
		}
		eng::Block<eng::LibSegmentTag> blk = eng::reserve<eng::LibSegmentTag>(
			mem, policy, static_cast<eng::u32>(image.size()), 4u);
		if (!blk.valid()) {
			l.state = LibState::Error;
			return false;
		}
		eng::u8* const dst = blk.view.data();
		for (eng::usize i = 0u; i < image.size(); ++i) {
			dst[i] = image[i];
		}
		const EngLibHeader& hdr = *reinterpret_cast<const EngLibHeader*>(dst);
		eng::u8* const code = dst + sizeof(EngLibHeader);
		const eng::u32 base = static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(code));
		auto* relocs = reinterpret_cast<eng::u32*>(code + hdr.code_size);
		for (eng::u16 i = 0u; i < hdr.reloc_count; ++i) {
			auto* cell = reinterpret_cast<eng::u32*>(code + relocs[i]);
			*cell = *cell + base;
		}
		l.image_block = static_cast<eng::Block<eng::LibSegmentTag>&&>(blk);
		l.owns_image = true;
		l.base = code;
		l.size = hdr.code_size;
		l.exports = reinterpret_cast<LibExport*>(relocs + hdr.reloc_count);
		l.export_count = hdr.export_count;
		l.fmt = LibFormat::EngLib;
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
		if (l.fmt == LibFormat::Hunk) {
			return l.hunk.symbol(name_hash);
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

	/// Descarga la lib (el llamador libera la imagen y/o la arena de segmentos).
	void unload(LibHandle h) noexcept {
		if (valid(h)) {
			m_libs[h - 1u] = Lib {};
		}
	}

	/// Descarga una lib **poseída** (`load(..., mem, ...)`): libera los segmentos (HUNK) o la imagen
	/// (`.englib`) al banco efectivo que les tocó.
	void unload(LibHandle h, MemoryManager& mem) noexcept {
		if (!valid(h)) {
			return;
		}
		Lib& l = m_libs[h - 1u];
		if (l.owns_hunk) {
			l.hunk.unload(mem);
		}
		if (l.owns_image && l.image_block.valid()) {
			eng::u8* const ptr = l.image_block.view.data();
			switch (l.image_block.kind) {
			case eng::MemoryKind::Chip: mem.chip().release(ptr); break;
			case eng::MemoryKind::Fast: mem.fast().release(ptr); break;
			default: mem.slow().release(ptr); break;
			}
		}
		l = Lib {};
	}

	/// Estado de la lib (`Empty` si el handle no es valido).
	[[nodiscard]] LibState state(LibHandle h) const noexcept {
		return valid(h) ? m_libs[h - 1u].state : LibState::Empty;
	}
	/// Formato del módulo cargado (`None` si no está listo).
	[[nodiscard]] LibFormat format(LibHandle h) const noexcept {
		return valid(h) ? m_libs[h - 1u].fmt : LibFormat::None;
	}
	[[nodiscard]] eng::u16 count() const noexcept { return m_count; }

	/// Hash FNV-1a de 32 bits del nombre de un símbolo (común a `.englib` y HUNK).
	[[nodiscard]] static constexpr eng::u32 hash_name(const char* s) noexcept { return symbol_hash(s); }

private:
	struct Lib {
		const char* path = nullptr;
		LibState state = LibState::Empty;
		LibFormat fmt = LibFormat::None;
		eng::u8* base = nullptr;
		eng::u32 size = 0u;
		LibExport* exports = nullptr;
		eng::u16 export_count = 0u;
		HunkImage hunk {};                              ///< segmentos + símbolos cuando es HUNK
		eng::Block<eng::LibSegmentTag> image_block {};  ///< imagen copiada cuando es `.englib` poseído
		bool owns_hunk = false;                         ///< segmentos reservados en `MemoryManager`
		bool owns_image = false;                        ///< imagen reservada en `MemoryManager`
	};

	[[nodiscard]] bool valid(LibHandle h) const noexcept { return h >= 1u && h <= m_count; }

	Lib m_libs[kMaxLibs] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::res
