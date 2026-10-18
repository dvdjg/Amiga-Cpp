#pragma once

/// \file room.hpp
/// **Contenido de sala** (`eng::adv`): la vista que el runtime consume de una sala — tamaño del
/// mundo, walkboxes y hotspots — sin saber de dónde viene (contenido convertido desde SCUMM o
/// escrito a mano). Es el destino natural de `SIM v2.rooms[]` vía el generador de contenido
/// (`tools/adventure/codegen/emit-content.mjs`) y el punto de entrada de A0/A1 de
/// `ROADMAP_ADVENTURE.md`.
///
/// La vista **no posee memoria**: es un `Span` sobre las tablas generadas (ROM) o cargadas
/// (RAM); el llamador decide. Cero heap, cero copias.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/adv/hotspot.hpp>
#include <eng/adv/types.hpp>
#include <eng/adv/walkbox.hpp>

namespace eng::adv {

/// Vista inmutable del contenido de una sala.
/// \tparam S escalar de coordenada (s16/s32/float).
template <class S>
struct RoomContent {
	RoomId id {kInvalidRoom};                        ///< id de sala (SIM `room`)
	eng::u16 width {0};                              ///< ancho del mundo en píxeles
	eng::u16 height {0};                             ///< alto del mundo en píxeles
	eng::Span<const Walkbox<S>> walkboxes {};        ///< cajas de suelo (orden de datos)
	eng::Span<const Hotspot<S>> hotspots {};         ///< hotspots clicables (orden de datos)

	/// ¿Contenido mínimo utilizable? (id válido y mundo con dimensiones positivas.)
	/// \return `true` si la sala se puede presentar y consultar.
	[[nodiscard]] constexpr bool ok() const noexcept {
		return id != kInvalidRoom && width > 0 && height > 0;
	}
};

} // namespace eng::adv
