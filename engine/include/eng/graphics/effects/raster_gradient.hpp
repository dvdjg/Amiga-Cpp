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

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/color.hpp>
#include <eng/core/math/arith.hpp>
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
	/// \param range  rango del degradado (bands/band_height/first).
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
		rebuild();
	}

	/// Copia la lista de colores clave (se recorta a `max_keys`).
	/// \param keys  colores clave (formato de registro de color).
	void set_keys(Span<const u16> keys) {
		const u16 n = keys.size() < max_keys ? static_cast<u16>(keys.size()) : max_keys;
		for (u16 i = 0; i < n; ++i) {
			m_keys[i] = keys[i];
		}
		m_key_count = n;
		rebuild();
	}

	/// Elige **ciclico** (la lista de claves se recorre en bucle) o **lineal** (extremos fijos);
	/// recalcula la tabla (setup).
	void set_cyclic(bool cyclic) {
		m_cyclic = cyclic;
		rebuild();
	}

	/// Desplaza el muestreo en unidades de clave (animacion del degradado).
	void set_phase(u16 phase) { m_phase = phase; }

	constexpr u16 bands() const { return m_range.bands; }
	constexpr u16 key_count() const { return m_key_count; }

	/// Rellena `out` con una intencion `PaletteLine` por banda y devuelve cuantas escribio
	/// (a lo sumo `cap`). Los colores viven en un buffer propio del efecto, estable hasta
	/// el siguiente `fill_intents`.
	/// \param out  buffer destino de intenciones `PaletteLine`.
	/// \return nº de intenciones escritas (a lo sumo `out.size()`).
	u16 fill_intents(eng::Span<graphics::CopperIntent> out) {
		const u16 cap = static_cast<u16>(out.size());
		if (out.empty() || m_key_count == 0u) {
			return 0u;
		}
		const u16 bands = m_range.bands;
		const u16 stride = static_cast<u16>(m_range.first) + 1u; // 1 o 2
		const u16* const row = m_table[phase_index()];           // fila precalculada (sin divisiones)
		u16 n = 0;
		for (u16 b = 0; b < bands && n < cap; ++b) {
			const u16 color = row[b];
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

	/// Tras **materializar** sus intenciones una vez (setup), casa cada banda con la palabra de
	/// DATO que el `Plan` registró (por registro+fila). Habilita la animación barata: a partir
	/// de aquí `patch_into` solo reescribe esas palabras, sin re-emitir la copperlist. Si el
	/// `Plan` no expone slots (`slot_count`), la animación por parcheo queda desactivada.
	template <typename Plan>
	void bind_slots(const Plan& plan) {
		for (u16 b = 0; b < m_range.bands; ++b) {
			m_slot[b] = no_slot;
			const u16 line = static_cast<u16>(m_range.first_line +
							 static_cast<u32>(b) * m_range.band_height);
			for (u16 j = 0; j < plan.slot_count(); ++j) {
				if (plan.slot_reg(j) == m_range.first && plan.slot_line(j) == line) {
					m_slot[b] = plan.slot_word(j);
					break;
				}
			}
		}
	}

	/// Actualiza los colores del degradado en la lista ya materializada (por frame): recalcula
	/// la fila de la fase (sin divisiones) y escribe **solo las palabras de dato** (coste ~0).
	/// Requiere `bind_slots` tras el `materialize` del setup.
	template <typename Plan>
	void patch_into(Plan& plan) const {
		if (m_key_count == 0u) {
			return;
		}
		const u16* const row = m_table[phase_index()];
		u16* const words = plan.active_words();
		for (u16 b = 0; b < m_range.bands; ++b) {
			if (m_slot[b] != no_slot) {
				words[m_slot[b]] = row[b];
			}
		}
	}

private:
	/// Indice de fase para la tabla (`phase % k`): **mascara** si `k` es potencia de dos, si no `%`
	/// (una sola vez por `fill_intents`, no por banda -> cumple la regla de coste).
	[[nodiscard]] u16 phase_index() const noexcept {
		if (m_pow2) {
			return static_cast<u16>(m_phase & m_pmask);
		}
		return (m_key_count != 0u) ? static_cast<u16>(m_phase % m_key_count) : 0u;
	}

	/// **Precalcula la tabla** `[fase][banda]` (una vez, en setup): `calc` para cada fase del ciclo
	/// (`0..k-1`) y banda. Aqui si hay divisiones, pero es **setup**, no bucle.
	void rebuild() {
		const u16 k = m_key_count;
		m_pow2 = (k != 0u) && ((k & static_cast<u16>(k - 1u)) == 0u);
		m_pmask = m_pow2 ? static_cast<u16>(k - 1u) : 0u;
		const u16 bands = m_range.bands;
		for (u16 p = 0u; p < k; ++p) {
			for (u16 b = 0u; b < bands; ++b) {
				m_table[p][b] = calc(p, b);
			}
		}
	}

	/// Color de la banda `b` en la fase `phase` (0..`k-1`). Es la **matematica original** de muestreo
	/// (interpola claves; lineal o ciclico); ahora solo la usa `rebuild` en **setup** para llenar
	/// `m_table`. El bucle (`fill_intents`) **no la llama** -> cero divisiones por frame.
	u16 calc(u16 phase, u16 b) const {
		const s32 k = m_key_count;
		const s32 bands = m_range.bands;
		s32 span = m_cyclic ? k : (k - 1);
		if (span < 1) {
			span = 1;
		}
		const s32 den = m_cyclic ? bands : (bands > 1 ? bands - 1 : 1);
		const s32 unum = static_cast<s32>(b) * span + static_cast<s32>(phase) * den;
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
	/// **Tabla precalculada** `[fase % k][banda]`: color de cada banda para cada fase del ciclo. La
	/// rellena `rebuild` (setup) y el bucle solo **copia** la fila de la fase — sin divisiones.
	/// Coste en RAM: `max_keys * max_bands * 2` bytes (2 KB con los maximos; menos si son menores).
	u16 m_table[max_keys][max_bands] {};
	/// `true` si `m_key_count` es **potencia de dos** -> el indice de fase es una **mascara** (`&`), no `%`.
	bool m_pow2 = false;
	/// Mascara `m_key_count - 1` cuando `m_pow2` (si no, 0).
	u16 m_pmask = 0;
	/// Palabra de DATO por banda en la copperlist ya materializada (`no_slot` si no casó). La
	/// fija `bind_slots` en setup; la usa `patch_into` para animar sin re-emitir.
	static constexpr u16 no_slot = 0xffffu;
	eng::util::Array<u16, max_bands> m_slot {};
};

} // namespace eng::graphics::effects
