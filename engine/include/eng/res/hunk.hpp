#pragma once

/// \file hunk.hpp
/// Lector/cargador **HUNK** (`eng::res`): el formato nativo de ejecutables de AmigaOS.
///
/// Un fichero HUNK es una secuencia lineal de **registros** (`[tag u32][payload]`). El
/// primero es `HUNK_HEADER`, con la tabla de tamaños por hunk; después vienen los hunks de
/// código/datos/BSS y, tras cada uno, sus tablas de **relocación** y símbolos, cerrando con
/// `HUNK_END`. El loader reserva los segmentos en una `eng::LinearArena` (sin heap), copia
/// el contenido, aplica las relocaciones e indexa los símbolos por hash FNV-1a.
///
/// ```text
///   HUNK_HEADER  [0][num][first][last][size×num]     ← tamaños (longs) + flags de memoria
///   HUNK_CODE    [num_longs][bytes...]  HUNK_RELOC32  [n][hunk][off...] 0  HUNK_END
///   HUNK_DATA    [num_longs][bytes...]  HUNK_SYMBOL   [nl][name][value]... 0  HUNK_END
///   HUNK_BSS     [num_longs]                                  (sin bytes)  HUNK_END
/// ```
///
/// Todo el formato está en **big-endian** (nativo m68k); las relocaciones suman la dirección
/// base del hunk destino a la celda indicada. Referencias: `dos/doshunks.h` (NDK) y
/// `docs/reference/amiga/techniques/` (HUNK). Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` §2.

#include <eng/core/byte_order.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/memory/arena.hpp>
#include <eng/res/symbol_hash.hpp>

namespace eng::res {

/// Códigos de registro HUNK (`dos/doshunks.h`, NDK). El tipo se lee de los bits bajos del
/// longword; los bits altos llevan los flags de memoria (`kHunkFChip`/`kHunkFFast`).
enum HunkTag : eng::u32 {
	HunkUnit = 999u,
	HunkName = 1000u,
	HunkCode = 1001u,
	HunkData = 1002u,
	HunkBss = 1003u,
	HunkReloc32 = 1004u,
	HunkReloc16 = 1005u,
	HunkReloc8 = 1006u,
	HunkExt = 1007u,
	HunkSymbol = 1008u,
	HunkDebug = 1009u,
	HunkEnd = 1010u,
	HunkHeader = 1011u,
	HunkOverlay = 1013u,
	HunkBreak = 1014u,
	HunkDrel32 = 1015u,
	HunkDrel16 = 1016u,
	HunkDrel8 = 1017u,
	HunkLib = 1018u,
	HunkIndex = 1019u,
	HunkReloc32Short = 1020u,
	HunkRelReloc32 = 1021u,
	HunkAbsReloc16 = 1022u,
};

/// Magic del primer longword de un ejecutable HUNK (`HUNK_HEADER`).
inline constexpr eng::u32 kHunkHeaderMagic = 0x000003F3u;
/// Flag de memoria: hunk **debe** ir en Chip RAM (bit 30).
inline constexpr eng::u32 kHunkFChip = 1u << 30;
/// Flag de memoria: hunk **prefiere** Fast RAM (bit 31).
inline constexpr eng::u32 kHunkFFast = 1u << 31;
/// Flag `ADVISORY` (bit 29): un tipo desconocido con este bit se ignora como `HUNK_DEBUG`.
inline constexpr eng::u32 kHunkFAdvisory = 1u << 29;
/// Máscara del código de tipo (limpia los bits de flags de memoria y advisory).
inline constexpr eng::u32 kHunkTypeMask = 0x1FFFFFFFu;
/// Máscara del tamaño en longs en la tabla de la cabecera (limpia los flags de memoria).
inline constexpr eng::u32 kHunkSizeMask = 0x3FFFFFFFu;

/// Política de memoria solicitada por un hunk (`HUNKF_CHIP`/`HUNKF_FAST`).
enum class HunkMem : eng::u8 { Any = 0, Chip, Fast };

/// Segmento cargado de un hunk: vista al bloque reservado en la arena del llamador.
struct HunkSegment {
	eng::u8* base = nullptr; ///< inicio del segmento (ya con relocaciones aplicadas)
	eng::u32 size = 0u;      ///< tamaño en bytes (longs × 4)
	HunkMem mem = HunkMem::Any;
	eng::u16 type = 0u; ///< `HunkCode` / `HunkData` / `HunkBss`
};

/// Símbolo indexado de un `HUNK_SYMBOL`: hash del nombre, hunk y offset dentro de él.
struct HunkSymbolEntry {
	eng::u32 name_hash = 0u;
	eng::u16 hunk = 0u;
	eng::u32 offset = 0u;
};

/// **Imagen HUNK cargada**: segmentos + índice de símbolos. No posee la memoria de los
/// segmentos (la reserva el llamador en su `LinearArena`). Ver `dynloader.hpp` para el
/// registro multi-módulo (`.englib` + HUNK).
class HunkImage {
public:
	static constexpr eng::u16 kMaxHunks = 8u;   ///< hunks por imagen (código/datos/BSS)
	static constexpr eng::u16 kMaxSymbols = 32u; ///< símbolos indexados por imagen

	/// Parsea `image` y reserva/copia sus hunks en `pool` (alineados a 4, BSS a cero),
	/// aplicando las relocaciones. `false` si el formato no es válido o la arena no cabe.
	/// El `image` puede liberarse tras la carga (los segmentos viven en `pool`).
	bool load(eng::Span<eng::u8> image, eng::LinearArena& pool) noexcept {
		m_count = 0u;
		m_symbol_count = 0u;
		const eng::u8* p = image.data();
		const eng::u8* const end = p + image.size();
		if (image.size() < 16u || eng::read_be32(p) != kHunkHeaderMagic) {
			return false;
		}
		p += 8; // magic + resident library list (siempre 0)
		const eng::u32 num = eng::read_be32(p);
		p += 4;
		const eng::u32 first = eng::read_be32(p);
		p += 4;
		const eng::u32 last = eng::read_be32(p);
		p += 4;
		if (num == 0u || num > kMaxHunks || first != 0u || last + 1u != num) {
			return false; // overlays y rangos raros no soportados
		}

		// Tabla de tamaños: reserva los segmentos ANTES de aplicar relocaciones, para que
		// todas las direcciones base sean conocidas al recorrer el cuerpo.
		for (eng::u32 i = 0u; i < num; ++i) {
			if (p + 4u > end) {
				return false;
			}
			const eng::u32 entry = eng::read_be32(p);
			p += 4;
			HunkMem mem = HunkMem::Any;
			if ((entry & (kHunkFChip | kHunkFFast)) == (kHunkFChip | kHunkFFast)) {
				if (p + 4u > end) {
					return false;
				}
				mem = mem_from_memf(eng::read_be32(p)); // flags MEMF_* extendidos
				p += 4;
			} else if ((entry & kHunkFChip) != 0u) {
				mem = HunkMem::Chip;
			} else if ((entry & kHunkFFast) != 0u) {
				mem = HunkMem::Fast;
			}
			const eng::u32 bytes = (entry & kHunkSizeMask) * 4u;
			const eng::MemoryBlock mb = pool.allocate(bytes != 0u ? bytes : 4u, 4u);
			if (!mb.valid()) {
				return false;
			}
			HunkSegment& s = m_hunks[i];
			s.base = static_cast<eng::u8*>(mb.data);
			s.size = bytes;
			s.mem = mem;
			s.type = 0u;
			++m_count;
			for (eng::u32 k = 0u; k < bytes; ++k) {
				s.base[k] = 0u; // BSS y cola no escrita quedan a cero
			}
		}

		// Cuerpo: rellena segmentos, aplica relocaciones e indexa símbolos.
		eng::u16 cur = 0u;
		while (p + 4u <= end) {
			const eng::u32 word = eng::read_be32(p);
			p += 4;
			const eng::u32 type = word & kHunkTypeMask;
			const eng::u32 flags = word & (kHunkFChip | kHunkFFast);
			if (type == HunkCode || type == HunkData || type == HunkBss) {
				if (cur >= m_count || p + 4u > end) {
					return false;
				}
				const eng::u32 longs = eng::read_be32(p);
				p += 4;
				if (flags == (kHunkFChip | kHunkFFast)) {
					if (p + 4u > end) {
						return false;
					}
					p += 4; // flags MEMF_* extendidos
				}
				HunkSegment& s = m_hunks[cur];
				s.type = static_cast<eng::u16>(type);
				const eng::u32 bytes = longs * 4u;
				if (bytes > s.size) {
					return false;
				}
				if (type != HunkBss) {
					if (p + bytes > end) {
						return false;
					}
					for (eng::u32 k = 0u; k < bytes; ++k) {
						s.base[k] = p[k];
					}
					p += bytes;
				}
			} else if (type == HunkReloc32 || type == HunkReloc16 || type == HunkReloc8 ||
			           type == HunkRelReloc32 || type == HunkAbsReloc16) {
				if (!reloc_long(cur, p, end, type)) {
					return false;
				}
			} else if (type == HunkReloc32Short || type == HunkDrel32 || type == HunkDrel16 ||
			           type == HunkDrel8) {
				if (!reloc_word(cur, p, end, type)) {
					return false;
				}
			} else if (type == HunkSymbol) {
				if (!parse_symbols(cur, p, end)) {
					return false;
				}
			} else if (type == HunkEnd) {
				++cur;
			} else if (type == HunkName || type == HunkDebug) {
				if (p + 4u > end) {
					return false;
				}
				const eng::u32 longs = eng::read_be32(p);
				p += 4;
				if (p + longs * 4u > end) {
					return false;
				}
				p += longs * 4u;
			} else if (type > HunkAbsReloc16) {
				if ((word & kHunkFAdvisory) == 0u && flags == 0u) {
					return false; // tipo desconocido sin advisory: no sabemos saltarlo
				}
				if (p + 4u > end) {
					return false;
				}
				const eng::u32 longs = eng::read_be32(p);
				p += 4;
				if (p + longs * 4u > end) {
					return false;
				}
				p += longs * 4u;
			} else {
				return false; // tipo conocido pero no soportado (EXT, overlay, LIB, ...)
			}
		}
		return cur == num;
	}

	/// Nº de segmentos cargados.
	[[nodiscard]] eng::u16 hunk_count() const noexcept { return m_count; }
	/// Segmento `i` (válido para `i < hunk_count()`).
	[[nodiscard]] const HunkSegment& hunk(eng::u16 i) const noexcept { return m_hunks[i]; }
	/// Nº de símbolos indexados.
	[[nodiscard]] eng::u16 symbol_count() const noexcept { return m_symbol_count; }

	/// Nº de **exports** enumerables (los símbolos indexados del módulo).
	[[nodiscard]] eng::u16 export_count() const noexcept { return m_symbol_count; }
	/// Hash del nombre del export `i` (0 si fuera de rango), para ligarlo por `symbol_hash`.
	[[nodiscard]] eng::u32 export_hash(eng::u16 i) const noexcept {
		return i < m_symbol_count ? m_symbols[i].name_hash : 0u;
	}
	/// Dirección del export `i` (base del hunk + offset), o `nullptr` si fuera de rango.
	/// Permite a un juego **enumerar y ligar** los exports del módulo sin conocer los nombres.
	[[nodiscard]] eng::u8* export_address(eng::u16 i) const noexcept {
		return i < m_symbol_count ? m_hunks[m_symbols[i].hunk].base + m_symbols[i].offset
					  : nullptr;
	}
	/// Punto de entrada por convención: base del primer hunk (código), o `nullptr`.
	[[nodiscard]] eng::u8* entry() const noexcept { return m_count != 0u ? m_hunks[0].base : nullptr; }

	/// Dirección del símbolo `name_hash` (base del hunk + offset), o `nullptr`.
	[[nodiscard]] eng::u8* symbol(eng::u32 name_hash) const noexcept {
		for (eng::u16 i = 0u; i < m_symbol_count; ++i) {
			if (m_symbols[i].name_hash == name_hash) {
				return m_hunks[m_symbols[i].hunk].base + m_symbols[i].offset;
			}
		}
		return nullptr;
	}
	/// Igual que `symbol(name_hash)` con el nombre en claro.
	[[nodiscard]] eng::u8* symbol(const char* name) const noexcept { return symbol(symbol_hash(name)); }

private:
	/// Traduce los flags `MEMF_*` de un hunk extendido a la política del engine.
	[[nodiscard]] static HunkMem mem_from_memf(eng::u32 memf) noexcept {
		if ((memf & 0x2u) != 0u) {
			return HunkMem::Chip; // MEMF_CHIP
		}
		if ((memf & 0x4u) != 0u) {
			return HunkMem::Fast; // MEMF_FAST
		}
		return HunkMem::Any;
	}

	/// Parchea la celda `off` del hunk `cur` sumando la base del hunk `target`, según el
	/// ancho y la semántica del tipo de relocación. `false` si el offset se sale del hunk.
	[[nodiscard]] bool patch(eng::u16 cur, eng::u32 off, eng::u32 target, eng::u32 type) const noexcept {
		if (cur >= m_count || target >= m_count) {
			return false;
		}
		const HunkSegment& s = m_hunks[cur];
		const eng::u32 base =
		    static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(m_hunks[target].base));
		eng::u8* const site = s.base + off;
		if (type == HunkReloc16 || type == HunkDrel16 || type == HunkAbsReloc16) {
			if (off + 2u > s.size) {
				return false;
			}
			eng::write_be16(site, static_cast<eng::u16>(eng::read_be16(site) + (base & 0xFFFFu)));
			return true;
		}
		if (type == HunkReloc8 || type == HunkDrel8) {
			if (off + 1u > s.size) {
				return false;
			}
			site[0] = static_cast<eng::u8>(site[0] + static_cast<eng::u8>(base & 0xFFu));
			return true;
		}
		if (off + 4u > s.size) {
			return false;
		}
		if (type == HunkRelReloc32) {
			// PC-relativa: desplazamiento firmado desde la celda hasta el destino.
			const eng::u32 cur_base =
			    static_cast<eng::u32>(reinterpret_cast<eng::uintptr>(s.base));
			eng::write_be32(site, eng::read_be32(site) + base - (cur_base + off + 4u));
			return true;
		}
		eng::write_be32(site, eng::read_be32(site) + base); // RELOC32 / DREL32 / RELOC32SHORT
		return true;
	}

	/// Tabla de relocación con campos de 32 bits (`count`, `hunk`, `offsets`), terminada en 0.
	[[nodiscard]] bool reloc_long(eng::u16 cur, const eng::u8*& p, const eng::u8* end,
	                              eng::u32 type) const noexcept {
		while (true) {
			if (p + 8u > end) {
				return false;
			}
			const eng::u32 count = eng::read_be32(p);
			p += 4;
			if (count == 0u) {
				return true;
			}
			const eng::u32 target = eng::read_be32(p);
			p += 4;
			for (eng::u32 i = 0u; i < count; ++i) {
				if (p + 4u > end) {
					return false;
				}
				const eng::u32 off = eng::read_be32(p);
				p += 4;
				if (!patch(cur, off, target, type)) {
					return false;
				}
			}
		}
	}

	/// Tabla de relocación con campos de 16 bits (`RELOC32SHORT`/`DREL*`), terminada en 0 y
	/// con relleno a longword si el total de words es impar.
	[[nodiscard]] bool reloc_word(eng::u16 cur, const eng::u8*& p, const eng::u8* end,
	                              eng::u32 type) const noexcept {
		eng::u32 words = 0u;
		while (true) {
			if (p + 2u > end) {
				return false;
			}
			const eng::u16 count = eng::read_be16(p);
			p += 2;
			++words;
			if (count == 0u) {
				break;
			}
			if (p + 2u > end) {
				return false;
			}
			const eng::u16 target = eng::read_be16(p);
			p += 2;
			++words;
			for (eng::u16 i = 0u; i < count; ++i) {
				if (p + 2u > end) {
					return false;
				}
				const eng::u16 off = eng::read_be16(p);
				p += 2;
				++words;
				if (!patch(cur, off, target, type)) {
					return false;
				}
			}
		}
		if ((words & 1u) != 0u) {
			if (p + 2u > end) {
				return false;
			}
			p += 2; // padding a longword
		}
		return true;
	}

	/// Tabla `HUNK_SYMBOL`: `[name_len_longs][name...][value]` hasta `name_len == 0`.
	/// Indexa cada símbolo (hash, hunk actual, offset) para resolver por hash.
	[[nodiscard]] bool parse_symbols(eng::u16 cur, const eng::u8*& p, const eng::u8* end) noexcept {
		while (true) {
			if (p + 4u > end) {
				return false;
			}
			const eng::u32 nl = eng::read_be32(p);
			p += 4;
			if (nl == 0u) {
				return true;
			}
			const eng::u32 nbytes = nl * 4u;
			if (p + nbytes + 4u > end) {
				return false;
			}
			const eng::u32 h = symbol_hash_n(p, nbytes);
			p += nbytes;
			const eng::u32 value = eng::read_be32(p);
			p += 4;
			if (m_symbol_count < kMaxSymbols) {
				HunkSymbolEntry& sym = m_symbols[m_symbol_count];
				sym.name_hash = h;
				sym.hunk = cur;
				sym.offset = value;
				++m_symbol_count;
			}
		}
	}

	HunkSegment m_hunks[kMaxHunks] {};
	HunkSymbolEntry m_symbols[kMaxSymbols] {};
	eng::u16 m_count = 0u;
	eng::u16 m_symbol_count = 0u;
};

} // namespace eng::res
