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

} // namespace eng::res
