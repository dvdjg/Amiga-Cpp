#pragma once

/// \file lod.hpp
/// **Nivel de detalle de simulación** (`eng::sim`): decide, según la distancia al jugador,
/// qué criaturas se simulan con detalle, cuáles de forma abstracta y cuáles quedan
/// **dormidas** (sin gastar CPU). Es lo que permite que el jugador perciba un mundo rico a
/// su alrededor mientras el resto del mundo apenas cuesta.
///
/// - `LodBand::Realized`: cerca del jugador, IA completa y percepción (el juego añade
///   física/render).
/// - `LodBand::Abstract`: a media distancia, tick abstracto barato (necesidades, olvido,
///   migración).
/// - `LodBand::Dormant`: lejos, congelada; no se actualiza hasta que el jugador se acerca.
///
/// `band_for` es puro: distancia y si comparten región. `SimWorld::update_lod` lo aplica a
/// todas las criaturas en cada frame, de modo que la carga se concentra alrededor del
/// jugador y se reparte fuera.
///
/// Verificación: HOST-174.

#include <eng/core/types.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Banda de detalle de una criatura.
enum class LodBand : eng::u8 {
	Realized = 0, ///< IA completa (cerca del jugador)
	Abstract = 1, ///< tick abstracto barato (media distancia)
	Dormant = 2,  ///< congelada, sin coste (lejos)
	Count = 3,
};

/// Radios de la banda por distancia (en celdas de mundo).
struct LodParams {
	eng::u8 realize_radius = 12u; ///< dentro: realizada
	eng::u8 abstract_radius = 28u; ///< dentro: abstracta; fuera: dormida
	eng::u8 room_hysteresis = 0u;  ///< margen extra si comparten región
};

/// Nombre legible (diagnóstico).
[[nodiscard]] constexpr const char* lod_band_name(LodBand b) noexcept {
	switch (b) {
		case LodBand::Realized: return "realized";
		case LodBand::Abstract: return "abstract";
		case LodBand::Dormant: return "dormant";
		default: return "?";
	}
}

/// Banda que corresponde a una criatura según su distancia al jugador y si comparte región.
[[nodiscard]] constexpr LodBand band_for(eng::u16 distance, bool same_room,
					 const LodParams& p = LodParams {}) noexcept {
	const eng::u8 rz = static_cast<eng::u8>(
		same_room ? u8_sat_add(p.realize_radius, 0u) : p.realize_radius);
	const eng::u8 ab = static_cast<eng::u8>(
		same_room ? u8_sat_add(p.abstract_radius, p.room_hysteresis) : p.abstract_radius);
	if (distance <= rz) {
		return LodBand::Realized;
	}
	if (distance <= ab) {
		return LodBand::Abstract;
	}
	return LodBand::Dormant;
}

/// ¿La banda cuesta CPU este frame?
[[nodiscard]] constexpr bool lod_costs_cpu(LodBand b) noexcept {
	return b != LodBand::Dormant;
}

} // namespace eng::sim
