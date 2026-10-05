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
#include <eng/debug/mem_probe.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/memory_manager.hpp>
#include <eng/os/file.hpp>

namespace eng::res {

/// Margen extra por reserva. **Ya no se usa** en los `load` con banco (`MemBank`): el `BlockPool`
/// alinea la base **una vez**, así que el padding no se acumula. Se conserva por si algún
/// consumidor de la arena *bump* (que sí acumula padding) lo necesita.
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

/// Refresca la **sonda** (`g_mem_probe`) con el estado del banco del dominio de `Tag` y registra
/// el fallo. La llama `load` cuando una reserva no cabe: es el punto único por el que pasan los
/// assets, así el diagnóstico queda poblado sin que cada demo instrumente a mano. Ver
/// `eng/debug/mem_probe.hpp` y `tools/debug/mem-probe.mjs`.
template <class Tag>
inline void probe_reserve_failure(MemoryManager& mm, u32 requested) noexcept {
	constexpr MemoryKind kind = DomainAsset<Tag>::kind;
	const auto refresh = [&](const auto& bank) {
		const auto snap = bank.snapshot();
		debug::refresh_mem_probe(kind, snap.capacity, snap.used, snap.remaining, snap.peak,
					 bank.block_count(), static_cast<u32>(bank.status()));
		debug::record_mem_failure(kind, requested, static_cast<u32>(bank.status()));
	};
	if (kind == MemoryKind::Chip) {
		refresh(mm.chip());
	} else if (kind == MemoryKind::Slow) {
		refresh(mm.slow());
	} else {
		refresh(mm.fast());
	}
}

/// **Carga tipada con los bancos** (`MemoryManager`): la puerta única. DMA
/// (`DomainAsset<Tag>::kind == Chip`) -> `MemBank<Chip>`; datos de CPU -> **Fast si la hay, si no
/// Slow** (`fast_or_slow`). Copia **una vez** (típicamente en `init`; no es camino caliente).
/// Devuelve un bloque inválido si `src` está vacío o no cabe (sin excepciones).
template <class Tag>
[[nodiscard]] inline Block<Tag> load(MemoryManager& mm, Span<const u8> src) {
	const u32 need = static_cast<u32>(src.size());
	if (need == 0u) {
		return {};
	}
	Block<Tag> block = (DomainAsset<Tag>::kind == MemoryKind::Chip)
				   ? Block<Tag> {mm.chip().reserve<Tag>(need, DomainAsset<Tag>::align)}
				   : fast_or_slow<Tag>(mm, need, DomainAsset<Tag>::align);
	if (!block.valid()) {
		// Punto único de fallo de carga de assets: deja el diagnóstico en `g_mem_probe`.
		probe_reserve_failure<Tag>(mm, need);
		return {};
	}
	eng::util::ByteReader reader {src};
	(void)reader.read_into(eng::Span<u8> {block.view.data(), need});
	return block;
}

/// **Carga un asset desde fichero** por la E/S **síncrona** del mini-SO (`os::file_*`): abre,
/// mide, reserva en el banco del dominio y lee. Escribe el tamaño útil en `out_bytes`. Devuelve
/// un bloque **inválido** si no se puede abrir/leer o no cabe. Debe llamarse **antes** del
/// `takeover_display` (`dos.library` necesita interrupciones).
template <class Tag>
[[nodiscard]] inline Block<Tag> load_file(MemoryManager& mm, const char* path, u32& out_bytes) {
	out_bytes = 0u;
	const eng::os::FileHandle h = eng::os::file_open(path, eng::os::FileMode::Read);
	if (h == 0u) {
		return {};
	}
	const u32 bytes = eng::os::file_size(h);
	Block<Tag> block = (DomainAsset<Tag>::kind == MemoryKind::Chip)
				   ? Block<Tag> {mm.chip().reserve<Tag>(bytes, DomainAsset<Tag>::align)}
				   : fast_or_slow<Tag>(mm, bytes, DomainAsset<Tag>::align);
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

/// Como arriba, sin pedir el tamaño útil.
template <class Tag>
[[nodiscard]] inline Block<Tag> load_file(MemoryManager& mm, const char* path) {
	u32 ignore = 0u;
	return load_file<Tag>(mm, path, ignore);
}

} // namespace eng::res
