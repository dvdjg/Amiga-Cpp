#pragma once

/// \file bitmap_view.hpp
/// **Zona rectangular de memoria gráfica** (no propietaria): planos + geometría + layout, con la
/// **memoria etiquetada** (tag de dominio + **banco** Chip/Fast). Es la abstracción común de todo lo
/// que representa un **área gráfica**: sprites de hardware, BOBs, zonas del framebuffer, viewports.
///
/// El **dueño** es un `gfx::Bitmap` (o un `Block<Tag, Bank>`); esta vista es la que **viaja** por las
/// APIs (como un `Span`, pero de zona gráfica). El **banco en el tipo** (`MemView<Tag, Bank>`) hace
/// que un área Chip **no** se pueda usar donde se exige Fast y al revés — y elegir el camino (Blitter
/// DMA vs C2P por CPU) según los tags. Ver `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §7.
///
/// Sustituye al `BobTarget` (`u8*` suelto): misma geometría (base/row_bytes/planos/layout) pero con
/// la **procedencia en el tipo**, sin `u8*`.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/plane_layout.hpp>

namespace eng::graphics {

/// Vista de una zona rectangular. `Bank = Chip` por defecto (el caso DMA: `BPLxPT`/Blitter/Copper).
template <class Tag, eng::MemoryKind Bank = eng::MemoryKind::Chip>
struct BitmapView {
	eng::MemView<Tag, Bank> planes {}; ///< memoria de la zona (tag + banco) + tamaño en bytes
	eng::u16 width = 0;                ///< ancho en píxeles
	eng::u16 height = 0;               ///< alto en píxeles
	eng::u16 row_bytes = 0;            ///< bytes por fila de UN plano (0 = width/8)
	eng::u8 plane_count = 0;           ///< profundidad (1..6)
	PlaneLayout layout = PlaneLayout::Interleaved;

	/// ¿Zona con memoria y geometría? (`planes` no vacío y ancho/alto/planos no nulos.)
	[[nodiscard]] constexpr bool valid() const noexcept {
		return !planes.empty() && width != 0u && height != 0u && plane_count != 0u;
	}
	/// Palabras por fila (`BLTSIZE` bajo): `row_bytes / 2`.
	[[nodiscard]] constexpr eng::u16 words_per_row() const noexcept {
		return static_cast<eng::u16>(row_bytes / 2u);
	}
	/// Bytes por fila del bitmap **completo**: `row_bytes × planos` si interleaved, `row_bytes` si
	/// separado.
	[[nodiscard]] constexpr eng::u32 bitmap_row_bytes() const noexcept {
		return layout == PlaneLayout::Interleaved
			       ? static_cast<eng::u32>(row_bytes) * plane_count
			       : row_bytes;
	}
	/// Separación en bytes entre planos (`Separate`); 0 en interleaved.
	[[nodiscard]] constexpr eng::u32 plane_stride() const noexcept {
		return layout == PlaneLayout::Separate ? static_cast<eng::u32>(row_bytes) * height : 0u;
	}
	/// ¿Planos interleaved (fila a fila) o contiguos (plano a plano)?
	[[nodiscard]] constexpr bool interleaved() const noexcept {
		return layout == PlaneLayout::Interleaved;
	}
	/// Nº de bytes que ocupa la zona (para validar la reserva).
	[[nodiscard]] constexpr eng::u32 byte_count() const noexcept {
		return static_cast<eng::u32>(row_bytes) * height * plane_count;
	}
};

/// Zona DMA (Chip): la que puede ver el Blitter/Copper (`BPLxPT`).
template <class Tag>
using ChipBitmapView = BitmapView<Tag, eng::MemoryKind::Chip>;
/// Zona de trabajo en Fast (p. ej. chunky antes de un C2P). No DMA.
template <class Tag>
using FastBitmapView = BitmapView<Tag, eng::MemoryKind::Fast>;

} // namespace eng::graphics
