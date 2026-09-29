#pragma once

/// \file load.hpp
/// **Carga tipada de assets** (`eng::res`): copia bytes a un `Block<Tag>` en la arena que
/// corresponde al dominio (Chip para lo que consume DMA, con su alineación), sustituyendo
/// el patrón repetido `allocate_block<Tag>(bytes + headroom, align)` + `memcpy`.
///
/// El medio y la alineación los fija `DomainAsset<Tag>` (una sola verdad por dominio), de
/// modo que el juego no elige a mano dónde vive un plano, un BOB o un módulo. Si no cabe,
/// `load` devuelve un bloque **inválido** (consulta antes con `res::Budget::can_fit`); no
/// hay excepciones.
///
/// ```cpp
/// auto bitmap = eng::res::load<eng::PlaneTag>(app.memory(), {g_img, img_bytes});
/// if (!bitmap.valid()) { /* no cupo */ }
/// ```
///
/// Es la mitad **síncrona** de `PUBLIC_GAME_API.md` §2.1.4 (fuente embebida/ya en RAM); la
/// variante de fichero asíncrona se apoya en `AssetCache` + `os::file_*` (backend Amiga).

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/util/binary.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/os/file.hpp>

namespace eng::res {

/// Margen extra por reserva: absorbe el padding de alineación del `LinearArena` cuando su
/// base no está alineada (ver «PEYOTE DE ALINEACIÓN» en `memory/arena.hpp`).
inline constexpr u32 kLoadHeadroom = 16u;

/// **Medio y alineación por defecto de un dominio de asset.** Los datos que consume DMA
/// (planos, BOB, música, samples, copper) van a Chip; los bitplanes y las copperlists piden
/// alineación a 16. El juego no elige esto a mano: lo fija el dominio.
template <class Tag>
struct DomainAsset {
	static constexpr MemoryKind kind = MemoryKind::Chip;
	static constexpr u32 align = 2u;
};
template <> struct DomainAsset<PlaneTag> {
	static constexpr MemoryKind kind = MemoryKind::Chip;
	static constexpr u32 align = 16u;
};
template <> struct DomainAsset<BobTag> {
	static constexpr MemoryKind kind = MemoryKind::Chip;
	static constexpr u32 align = 16u;
};
template <> struct DomainAsset<CopperTag> {
	static constexpr MemoryKind kind = MemoryKind::Chip;
	static constexpr u32 align = 16u;
};
template <> struct DomainAsset<MusicTag> {
	static constexpr MemoryKind kind = MemoryKind::Chip;
	static constexpr u32 align = 4u;
};
template <> struct DomainAsset<AudioTag> {
	static constexpr MemoryKind kind = MemoryKind::Chip;
	static constexpr u32 align = 4u;
};

/// Arena del `MemorySystem` que corresponde a `kind`. `Fast` se sirve de la arena `slow`
/// (el engine no mantiene una arena Fast propia).
[[nodiscard]] inline LinearArena& arena_for(MemorySystem& mem, MemoryKind kind) noexcept {
	switch (kind) {
		case MemoryKind::Slow:
		case MemoryKind::Fast:
			return mem.slow;
		default:
			return mem.chip;
	}
}

/// **Carga `src` en un `Block<Tag>`** de la arena y alineación indicadas. Copia **una vez**
/// (típicamente en `init`; no es camino caliente). Devuelve un bloque inválido si `src`
/// está vacío o no cabe (la reserva real decide, sin excepciones).
template <class Tag>
[[nodiscard]] inline Block<Tag> load(MemorySystem& mem, Span<const u8> src, MemoryKind kind,
				     u32 align) {
	const u32 need = static_cast<u32>(src.size());
	if (need == 0u) {
		return {};
	}
	Block<Tag> block =
		arena_for(mem, kind).allocate_block<Tag>(static_cast<u32>(need + kLoadHeadroom), align);
	if (!block.valid()) {
		return {};
	}
	eng::util::ByteReader reader {src};
	(void)reader.read_into(eng::Span<u8> {block.view.data(), need});
	return block;
}

/// Como arriba, con el medio y la alineación del dominio (`DomainAsset<Tag>`). Es la puerta
/// normal: `res::load<eng::PlaneTag>(memory, bytes)`.
template <class Tag>
[[nodiscard]] inline Block<Tag> load(MemorySystem& mem, Span<const u8> src) {
	return load<Tag>(mem, src, DomainAsset<Tag>::kind, DomainAsset<Tag>::align);
}

/// Carga tipada con los **bancos** (`MemoryManager`): DMA (`DomainAsset<Tag>::kind == Chip`) ->
/// `MemBank<Chip>` (tipado, compile-time); datos de CPU -> **Fast si la hay, si no Slow**
/// (`fast_or_slow`). Es la puerta para datos que la CPU procesa intensivamente.
template <class Tag>
[[nodiscard]] inline Block<Tag> load(MemoryManager& mm, Span<const u8> src) {
	const u32 need = static_cast<u32>(src.size());
	if (need == 0u) {
		return {};
	}
	Block<Tag> block = (DomainAsset<Tag>::kind == MemoryKind::Chip)
				   ? Block<Tag> {mm.chip().reserve<Tag>(need + kLoadHeadroom,
									DomainAsset<Tag>::align)}
				   : fast_or_slow<Tag>(mm, need + kLoadHeadroom, DomainAsset<Tag>::align);
	if (!block.valid()) {
		return {};
	}
	eng::util::ByteReader reader {src};
	(void)reader.read_into(eng::Span<u8> {block.view.data(), need});
	return block;
}

/// **Carga un asset desde fichero** por la E/S **síncrona** del mini-SO (`os::file_*`):
/// abre, mide, reserva en la arena del dominio y lee. Escribe el tamaño útil en
/// `out_bytes` (el bloque lleva margen de alineación, así que su `view().size()` es mayor).
/// Devuelve un bloque **inválido** si no se puede abrir/leer o no cabe. Debe llamarse
/// **antes** del `takeover_display` (`dos.library` necesita interrupciones).
template <class Tag>
[[nodiscard]] inline Block<Tag> load_file(MemorySystem& mem, const char* path, u32& out_bytes,
					  MemoryKind kind, u32 align) {
	out_bytes = 0u;
	const eng::os::FileHandle h = eng::os::file_open(path, eng::os::FileMode::Read);
	if (h == 0u) {
		return {};
	}
	const u32 bytes = eng::os::file_size(h);
	Block<Tag> block =
		arena_for(mem, kind).allocate_block<Tag>(static_cast<u32>(bytes + kLoadHeadroom), align);
	if (!block.valid()) {
		eng::os::file_close(h);
		return {};
	}
	const eng::s32 got =
		eng::os::file_read_sync(h, eng::Span<u8> {block.view.data(), bytes}, 0u);
	eng::os::file_close(h);
	if (got < 0 || static_cast<u32>(got) != bytes) {
		return {};
	}
	out_bytes = bytes;
	return block;
}

/// Como arriba, con el medio y la alineación del dominio (`DomainAsset<Tag>`), pero
/// devolviendo además el tamaño útil en `out_bytes`.
template <class Tag>
[[nodiscard]] inline Block<Tag> load_file(MemorySystem& mem, const char* path, u32& out_bytes) {
	return load_file<Tag>(mem, path, out_bytes, DomainAsset<Tag>::kind, DomainAsset<Tag>::align);
}

/// Como arriba, con el medio y la alineación del dominio (`DomainAsset<Tag>`).
template <class Tag>
[[nodiscard]] inline Block<Tag> load_file(MemorySystem& mem, const char* path) {
	u32 ignore = 0u;
	return load_file<Tag>(mem, path, ignore, DomainAsset<Tag>::kind, DomainAsset<Tag>::align);
}

} // namespace eng::res
