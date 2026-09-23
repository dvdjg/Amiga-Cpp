#pragma once

/// \file raster_gradient.hpp
/// Efecto grafico reutilizable: **degradado por banda/línea** (`COLORxx` por franja).
///
/// Reparte `bands` cambios de color sobre un rango vertical, interpolando una lista de
/// **colores clave**. Es la forma barata del "raster gradient" clasico: en vez de un
/// `WAIT`+`MOVE` por scanline, se cambia `COLORxx` cada `band_height` líneas. Con el
/// `copper::Plan`, el llamante solo aporta las intenciones y el plan las ordena por
/// scanline, resuelve conflictos y presupuesta con el `Timeline`.
///
/// Salida como **`graphics::CopperIntent`** (`PaletteLine`), pensada para
/// `copper::Plan::add` o `Scheduler::emit_copper_intents`. El efecto no escribe
/// registros ni conoce DMA.
///
/// ## Modos
///
/// - **Cíclico** (`cyclic = true`, por defecto): la lista de claves se recorre en bucle
///   y se interpola entre la última y la primera. Sirve para cielos/arcoíris continuos.
/// - **Lineal** (`cyclic = false`): primera clave en la banda `0` y última en `bands-1`,
///   sin salto de vuelta. Sirve para un amanecer/horizonte con extremos fijos.
///
/// `phase` desplaza el muestreo **en unidades de clave** (animacion): con tantas claves
/// como bandas y `phase` entero, cada banda toma una clave distinta y el degradado
/// "rueda" (equivale al color cycling por banda).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/color.hpp>
#include <eng/core/arith.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics::effects {

/// Descriptor del degradado: rango vertical, tamano de banda y registro de arranque.
struct RasterGradientRange {
	u16 first_line = 0;  ///< primera línea del degradado
	u16 band_height = 1; ///< líneas por banda (1 = por scanline)
	u8  bands = 0;       ///< número de bandas (0 se corrige a 1)
	u8  first = 0;       ///< registro `COLORxx` (0 o 1); la vista abarca `first+1`
};

/// Efecto de degradado por banda, sin asignaciones dinamicas.
class RasterGradientEffect {
public:
	static constexpr u16 max_bands = 64;
	static constexpr u16 max_keys = 16;

	/// Configura el rango. Recorta de forma segura `bands`/`band_height`/`first`.
	void configure(RasterGradientRange range) {
		if (range.bands == 0u) {
			range.bands = 1u;
		}
		if (range.bands > max_bands) {
			range.bands = static_cast<u8>(max_bands);
		}
		if (range.band_height == 0u) {
			range.band_height = 1u;
		}
		if (range.first > 1u) {
			range.first = 1u;
		}
		m_range = range;
	}

	/// Copia la lista de colores clave (se recorta a `max_keys`).
	void set_keys(Span<const u16> keys) {
		const u16 n = keys.size() < max_keys ? static_cast<u16>(keys.size()) : max_keys;
		for (u16 i = 0; i < n; ++i) {
			m_keys[i] = keys[i];
		}
		m_key_count = n;
	}

	void set_cyclic(bool cyclic) { m_cyclic = cyclic; }

	/// Desplaza el muestreo en unidades de clave (animacion del degradado).
	void set_phase(u16 phase) { m_phase = phase; }

	constexpr u16 bands() const { return m_range.bands; }
	constexpr u16 key_count() const { return m_key_count; }

	/// Rellena `out` con una intencion `PaletteLine` por banda y devuelve cuantas escribio
	/// (a lo sumo `cap`). Los colores viven en un buffer propio del efecto, estable hasta
	/// el siguiente `fill_intents`.
	u16 fill_intents(eng::Span<graphics::CopperIntent> out) {
		const u16 cap = static_cast<u16>(out.size());
		if (out.empty() || m_key_count == 0u) {
			return 0u;
		}
		const u16 bands = m_range.bands;
		const u16 stride = static_cast<u16>(m_range.first) + 1u; // 1 o 2
		u16 n = 0;
		for (u16 b = 0; b < bands && n < cap; ++b) {
			const u16 color = sample(b);
			const u16 slot = static_cast<u16>(b * 2u);
			m_colors[static_cast<u16>(slot + m_range.first)] = color;
			graphics::CopperIntent& it = out[n];
			it = graphics::CopperIntent {};
			it.kind = graphics::CopperIntentKind::PaletteLine;
			it.top = static_cast<u16>(m_range.first_line + static_cast<u32>(b) * m_range.band_height);
			it.bottom = it.top;
			it.hpos = 0;
			it.colors = eng::PaletteWords {&m_colors[slot], stride};
			it.first = m_range.first;
			it.count = 1;
			++n;
		}
		return n;
	}

	/// Aporta el degradado al `Plan` (cualquiera con `add(CopperIntent*, u16)`, p. ej.
	/// `eng::copper::Plan`). Es el metodo del concepto `Effect<RasterGradientEffect, Plan>`.
	template <typename Plan>
	void apply_into(Plan& plan) {
		const u16 n = fill_intents(m_intents);
		plan.add(m_intents, n);
	}

private:
	/// Color de la banda `b`: interpola las claves en la posicion `b + phase` (en unidades
	/// de clave), con o sin vuelta.
	u16 sample(u16 b) const {
		const s32 k = m_key_count;
		const s32 bands = m_range.bands;
		s32 span = m_cyclic ? k : (k - 1);
		if (span < 1) {
			span = 1;
		}
		const s32 den = m_cyclic ? bands : (bands > 1 ? bands - 1 : 1);
		const s32 unum = static_cast<s32>(b) * span + static_cast<s32>(m_phase) * den;
		const s16 seg_raw = eng::math::div_wide(unum, static_cast<s16>(den));
		const u16 local = static_cast<u16>(unum - static_cast<s32>(seg_raw) * den);
		s32 seg = seg_raw % k;
		if (seg < 0) {
			seg += k;
		}
		s32 next = seg + 1;
		if (m_cyclic) {
			if (next >= k) {
				next = 0;
			}
		} else if (next >= k) {
			next = k - 1;
		}
		return eng::util::lerp444(m_keys[seg], m_keys[next], local, static_cast<u16>(den));
	}

	RasterGradientRange m_range {};
	eng::util::Array<u16, max_keys> m_keys {};
	u16 m_key_count = 0;
	u16 m_phase = 0;
	bool m_cyclic = true;
	graphics::CopperIntent m_intents[max_bands] {};
	/// Dos slots por banda para que la vista `colors[first]` sea valida con `first` = 0 o 1.
	eng::util::Array<u16, max_bands * 2u> m_colors {};
};

} // namespace eng::graphics::effects
