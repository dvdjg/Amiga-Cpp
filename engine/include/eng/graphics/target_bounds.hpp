#pragma once

/// \file target_bounds.hpp
/// **Límites de memoria de un destino de blits** (`TargetBounds`): la zona Chip de un bitmap
/// con su tamaño útil. Viaja con el `FramePlan` para que **cualquier** trabajo —también los
/// construidos a mano— se valide contra la reserva antes de programar el Blitter, sin que el
/// creador del trabajo tenga que acordarse (`blit_job_within`).
///
/// Un `TargetBounds` **inactivo** (`bytes == 0`) significa «sin límites»: es el caso de los
/// planes que no conocen su destino (tests, herramientas). La composición de escena lo activa
/// con la geometría real (`BitmapView::bound()` desde `Scene::draw_target`).

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Reserva DMA (Chip) de un destino y su tamaño en bytes.
struct TargetBounds {
	eng::Address<eng::MemoryKind::Chip> base {}; ///< base del bloque (plano 0)
	eng::u32 bytes = 0u;                         ///< bytes útiles; 0 = sin límites

	/// `true` si hay límites declarados (el plan valida).
	[[nodiscard]] constexpr bool active() const noexcept { return bytes != 0u; }

	/// ¿El rango `[off, off+len)` cae dentro de la reserva? (`off` en bytes desde `base`).
	[[nodiscard]] constexpr bool contains(eng::u32 off, eng::u32 len) const noexcept {
		return !active() || (off <= bytes && len <= bytes - off);
	}
	/// Igual, con un desplazamiento **con signo**: un negativo queda fuera.
	[[nodiscard]] constexpr bool contains(eng::s32 off, eng::u32 len) const noexcept {
		return !active() || (off >= 0 && contains(static_cast<eng::u32>(off), len));
	}
};

} // namespace eng::graphics
