#pragma once

/// \file palette_transition.hpp
/// Efecto grafico reutilizable: **transicion/fundido de paleta** (cross-fade entre dos
/// paletas sobre una franja de registros `COLORxx`).
///
/// Es el hermano de `PaletteCycleEffect`: donde aquel **rota** un tramo, este **interpola**
/// entre dos paletas cocinadas. Un fundido a negro es simplemente una transicion cuyo
/// destino tiene los colores a cero (`palette_scale` de `eng::util::color`).
///
/// Igual que el resto de efectos, cumple el concepto `eng::graphics::Effect` (ver
/// `raster_intent.hpp`): el juego llama `update(frame)` y `apply_into(plan)`; el efecto
/// aporta un parche base de paleta al `FramePlan` y el driver decide como materializarlo
/// (parche de copperlist, CPU, doble buffer...). No escribe registros ni conoce DMA.
///
/// Composicion: como cada efecto registra un parche con su propio `first/count`, se pueden
/// combinar varios (p. ej. ciclo en `1..7` + transicion en `16..31`) y el driver los
/// escribe sin solaparse.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/color.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics::effects {

/// Descriptor de la franja de paleta que se transiciona y su ritmo.
struct PaletteTransitionRange {
	u8 first = 0;        // primer registro COLORxx (0..31)
	u8 count = 32;       // numero de colores
	u16 frames = 64;     // frames por sentido (0 se corrige a 1)
	bool ping_pong = true; // ida y vuelta; false = una sola vez y se queda en `to`
	/// Si `>= 0`, el parche es de **zona** (cambia la paleta a partir de esa linea raster,
	/// como `Palette32Zone`); si es negativo, es de **base** (`COLORxx` globales).
	s16 zone_line = -1;
};

/// Efecto de transicion de paleta sin asignaciones dinamicas.
///
/// El estado temporal es `num/den` en `[0,1]`: `num == 0` deja la paleta `from` y
/// `num == den` la deja `to`. `apply_into` escribe la paleta runtime (base `from` con el
/// tramo interpolado) y registra el parche en el plan.
class PaletteTransitionEffect {
public:
	constexpr PaletteTransitionEffect() = default;

	/// Configura la franja y el ritmo. Los rangos fuera de los 32 registros fisicos se
	/// recortan de forma segura.
	void configure(PaletteTransitionRange range) {
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
		if (range.frames == 0u) {
			range.frames = 1u;
		}
		m_range = range;
		m_num = 0;
		m_den = range.frames;
	}

	/// Vincula las paletas cocinadas: `from` en `num == 0`, `to` en `num == den`. El
	/// efecto no las modifica; produce su propia paleta runtime.
	void bind(const eng::Palette32& from, const eng::Palette32& to) {
		m_from = from.color;
		m_to = to.color;
		m_runtime = from;
	}

	/// Avanza el estado temporal. Calcula `num/den`: triangular (ida y vuelta) si
	/// `ping_pong`, o `0..den` y se **queda** en `den` (destino) si no.
	void update(u16 frame_index) {
		const u32 f = m_range.frames;
		u32 pos = frame_index;
		if (m_range.ping_pong) {
			pos %= (2u * f);
			if (pos >= f) {
				pos = 2u * f - pos;
			}
		} else if (pos > f) {
			pos = f;
		}
		m_num = static_cast<u16>(pos);
		m_den = static_cast<u16>(f);
	}

	/// Paleta runtime derivada (base `from` con el tramo interpolado hacia `to`). Se
	/// refresca en `apply_into` (igual que `PaletteCycleEffect`): llámala después de
	/// aplicarla al plan, o usa el puntero estable para el `scene_config` del driver.
	constexpr const eng::Palette32& runtime_palette() const { return m_runtime; }

	/// Recalcula `runtime_palette()` desde el estado actual (`bind`+`update`) **sin**
	/// tocar un `FramePlan`: para quien parchea la paleta por handles (`composition::PatchZone`).
	void refresh() { apply_fixed(); }

	constexpr u16 num() const { return m_num; }
	constexpr u16 den() const { return m_den; }
	constexpr PaletteTransitionRange range() const { return m_range; }

	/// Aporta este efecto al plan: interpola `m_from -> m_to` en la paleta runtime y
	/// registra el parche (base o de zona segun `zone_line`). Es el metodo del concepto
	/// `Effect<PaletteTransitionEffect, FramePlan>`.
	void apply_into(FramePlan& plan) {
		if (m_from == nullptr || m_to == nullptr) {
			return;
		}
		apply_fixed();
		if (m_range.zone_line < 0) {
			plan.add_base_palette_patch(m_runtime, m_range.first, m_range.count);
		} else {
			plan.add_zone_palette_patch(static_cast<u8>(m_range.zone_line), m_runtime,
						    m_range.first, m_range.count);
		}
	}

private:
	void apply_fixed() {
		for (u8 i = 0; i < 32u; ++i) {
			m_runtime.color[i] = m_from[i];
		}
		eng::util::palette_lerp(
			eng::Span<eng::u16> {m_runtime.color + m_range.first, m_range.count},
			eng::Span<const eng::u16> {m_from + m_range.first, m_range.count},
			eng::Span<const eng::u16> {m_to + m_range.first, m_range.count},
			m_num, m_den);
	}

	PaletteTransitionRange m_range { 0, 32, 64, true };
	u16 m_num = 0;
	u16 m_den = 64;
	const u16* m_from = nullptr;
	const u16* m_to = nullptr;
	eng::Palette32 m_runtime {};
};

// Evidencia del contrato: el efecto cumple `Effect<E, FramePlan>` como su hermano.
static_assert(Effect<PaletteTransitionEffect, FramePlan>);

} // namespace eng::graphics::effects
