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
	/// Paso explícito entre planos en bytes (0 = derivarlo de `layout`/`row_bytes`/`height`). Sirve
	/// a geometrías no estándar (p. ej. dual playfield de un bitmap: planos de PF1 intercalados con
	/// PF2 cada `2 × row_bytes`, que la fórmula `row × height` no produce).
	eng::u32 plane_step = 0;

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
	/// **Paso entre punteros de plano** (`BPLxPT`): `row_bytes` si interleaved (los planos van
	/// palabra a palabra por línea) o `row_bytes × height` si separados (plano contiguo).
	[[nodiscard]] constexpr eng::u32 plane_pointer_step() const noexcept {
		return plane_step != 0u
			       ? plane_step
			       : (interleaved() ? row_bytes
						: static_cast<eng::u32>(row_bytes) * height);
	}
	/// ¿Planos interleaved (fila a fila) o contiguos (plano a plano)?
	[[nodiscard]] constexpr bool interleaved() const noexcept {
		return layout == PlaneLayout::Interleaved;
	}
	/// **Base del plano 0 (fila 0) mutable**: es el **destino** natural de un Blit. Sale del
	/// `Address<Bank>` (`ptr()`), el escape explícito de la frontera. Es un `const`-method como un
	/// puntero-miembro: la vista no cambia, el almacén sí puede escribirse.
	[[nodiscard]] constexpr eng::u8* data() const noexcept { return planes.address(0).ptr(); }
	/// Base **constante** (el caso origen: fuente de un Blit/Copper).
	[[nodiscard]] constexpr const eng::u8* cdata() const noexcept { return planes.data(); }
	/// Dirección tipada por el banco del plano `p` (`off` en bytes extra): para `BPLxPT`.
	[[nodiscard]] constexpr eng::Address<Bank> plane_address(eng::u8 p, eng::s32 off = 0) const noexcept {
		return planes.address(static_cast<eng::s32>(plane_pointer_step()) * p + off);
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
/// Zona en Slow RAM (Agnus no la ve: **no** es DMA-visible, como Fast). Se declara por completitud
/// del medio; su uso normal es trabajo de CPU con el banco explícito en el tipo.
template <class Tag>
using SlowBitmapView = BitmapView<Tag, eng::MemoryKind::Slow>;

} // namespace eng::graphics
