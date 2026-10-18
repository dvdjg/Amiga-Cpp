// GENERADO por tools/adventure/codegen/emit-content.mjs — NO editar a mano.
// Fuente: out/tmp/codegen-fixture/decoded/sim.json (SIM v2, motor scumm-v5).
// Regenerar: node tools/adventure/codegen/emit-content.mjs codegen-fixture
#pragma once

#include <eng/adv/room.hpp>

namespace fixture {

namespace content {

/// Sala 1 «test-room»: 320x200, 1 walkboxes, 1 hotspots.
inline constexpr eng::adv::Walkbox<eng::s16> kRoom1Walkboxes[] = {
	{ {{0, 0}}, {{64, 0}}, {{64, 64}}, {{0, 64}}, 0x0f, 0x00, 255 },
};

/// Hotspots de la sala 1 «test-room»: 320x200, 1 walkboxes, 1 hotspots.
inline constexpr eng::adv::Hotspot<eng::s16> kRoom1Hotspots[] = {
	{ {{40, 48}}, {{55, 63}}, 1u },
};

/// Índice de salas con contenido (1 de 1); lookup lineal.
inline constexpr eng::adv::RoomContent<eng::s16> kRooms[] = {
	{1u, 320u, 200u, kRoom1Walkboxes, kRoom1Hotspots},
};

/// Busca el contenido de una sala por id (escaneo lineal; son decenas de salas).
/// \param id id de sala (SIM `room`).
/// \return puntero al contenido, o `nullptr` si el juego no tiene esa sala.
[[nodiscard]] constexpr const eng::adv::RoomContent<eng::s16>* find_room(eng::adv::RoomId id) noexcept {
	for (const eng::adv::RoomContent<eng::s16>& room : kRooms) {
		if (room.id == id) return &room;
	}
	return nullptr;
}

} // namespace content

} // namespace fixture
