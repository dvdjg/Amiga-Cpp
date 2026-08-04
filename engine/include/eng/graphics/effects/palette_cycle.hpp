#pragma once

/// \file palette_cycle.hpp
/// Primer efecto grafico reutilizable del engine: ciclo de paleta.
///
/// En Amiga, cambiar colores suele ser mucho mas barato que redibujar pixels. Un
/// ciclo de paleta permite animar agua, fuego, luces, neones o maquinaria dejando
/// los bitplanes quietos y modificando solo varios registros `COLORxx`.
///
/// La API esta pensada para mantenerse portable:
///
/// - la logica de juego no sabe nada de registros custom;
/// - el efecto trabaja sobre una paleta fisica de 32 colores;
/// - el driver/scheduler decidiran si esos cambios se escriben en Copper, CPU,
///   doble buffer de copperlist o incluso otro backend futuro.

#include <eng/core/types.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>

namespace eng::graphics::effects {

/// Descriptor de un tramo circular dentro de una paleta EHB.
///
/// `first` y `count` seleccionan colores fisicos `COLOR00..COLOR31`. En EHB, los
/// indices half-brite derivados tambien cambian automaticamente porque dependen de
/// esos mismos registros base.
struct PaletteCycleRange {
	u8 first = 0;
	u8 count = 0;
	u8 speed_frames = 1;
};

/// Efecto de ciclo de paleta sin asignaciones dinamicas.
class PaletteCycleEffect {
public:
	constexpr PaletteCycleEffect() = default;

	/// Configura el tramo que se va a rotar.
	///
	/// Un `speed_frames` de 1 avanza cada frame; valores mayores hacen la animacion
	/// mas lenta. Si `first/count` salen de los 32 registros fisicos, se recortan de
	/// forma segura.
	void configure(PaletteCycleRange range) {
		if (range.first >= 32u) {
			range.first = 31u;
			range.count = 1u;
		}
		if (range.count == 0u) {
			range.count = 1u;
		}
		if (range.first + range.count > 32u) {
			range.count = static_cast<u8>(32u - range.first);
		}
		if (range.speed_frames == 0u) {
			range.speed_frames = 1u;
		}

		m_range = range;
		m_phase = 0;
	}

	/// Avanza el estado temporal del efecto.
	void update(u16 frame_index) {
		m_phase = static_cast<u8>((frame_index / m_range.speed_frames) % m_range.count);
	}

	/// Copia `source` en `destination` aplicando la rotacion configurada.
	///
	/// El efecto no modifica la paleta original. Esto es importante para un motor
	/// retained-mode: los assets cocinados permanecen inmutables y cada frame se
	/// generan vistas/runtime state baratas.
	void apply(
		const drivers::EhbPalette& source,
		drivers::EhbPalette& destination
	) const {
		for (u8 i = 0; i < 32u; ++i) {
			destination.color[i] = source.color[i];
		}

		for (u8 i = 0; i < m_range.count; ++i) {
			const u8 source_index = static_cast<u8>(
				m_range.first + ((i + m_phase) % m_range.count)
			);
			destination.color[m_range.first + i] = source.color[source_index];
		}
	}

	constexpr PaletteCycleRange range() const { return m_range; }
	constexpr u8 phase() const { return m_phase; }

private:
	PaletteCycleRange m_range { 0, 1, 1 };
	u8 m_phase = 0;
};

} // namespace eng::graphics::effects
