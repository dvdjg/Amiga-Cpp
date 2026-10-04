#pragma once

/// \file sprite_band.hpp
/// **Reparto híbrido de los 8 canales de sprite por franjas de raster**.
///
/// El chipset OCS/ECS/AGA tiene 8 canales de sprite reutilizables **verticalmente**
/// (un canal dibuja cosas distintas en franjas separadas) y **horizontalmente** (un
/// canal se reposiciona varias veces dentro de la misma línea). Este header modela el
/// **recurso compartido** que hace posible mezclar técnicas distintas por franja:
///
///   - `SpriteBand`: una banda de raster que reclama una corrida de canales para su
///     **fondo** por sprites (`Layer`, `RiskyWoods`, `FreeForm`).
///   - `SpriteChannelLedger`: ocupación **canal × intervalo de líneas**. Los fondos la
///     llenan con `occupy_run`; el `SpriteAllocator` la consulta para repartir los
///     **objetos** solo en los canales libres de su franja.
///   - `plan_sprite_bands`: valida las bandas y las vuelca al ledger.
///
/// Ejemplo de uso (el caso híbrido que motiva el diseño): un fondo *Risky Woods* usa 6
/// canales en la franja `[60,100)` y deja los canales 6 y 7 libres para objetos; por
/// encima y por debajo de la banda, los 8 canales vuelven a estar disponibles:
///
/// ```text
///   canal 0..5  ├──── fondo Risky Woods [60,100) ────┤
///   canal 6..7  (libres)  ← 2 canales para objetos en la franja
/// ```
///
/// Es lógica **pura** (sin hardware, sin heap, sin STL): host-testable. El driver de
/// cada técnica de fondo (`effects::SpriteLayer` y los futuros `RiskyWoods`/`FreeForm`)
/// es quien emite los MOVEs de Copper; aquí solo se reserva y se consulta el recurso.
/// Diseño completo: `docs/engine/architecture/SPRITE_BANDS.md`.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>

namespace eng::graphics {

/// **Técnica de fondo por sprites** de una banda de raster.
///
/// Es una etiqueta de intención para el planner y los drivers; el ledger solo usa la
/// cuenta de canales y el rango de líneas. La técnica determina cómo el driver emite
/// la copperlist de la franja (ver `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`).
enum class SpriteBackdropTechnique : u8 {
	None = 0,    ///< la banda no usa sprites para el fondo (no reserva canales)
	Layer,       ///< N canales contiguos, un patrón de 16 px por canal (`effects::SpriteLayer`)
	RiskyWoods,  ///< reposición horizontal repetida: patrón de 64 px (15 colores con attached)
	FreeForm,    ///< reposición + DATA distinta por columna: fondo libre, sin patrón repetido
};

/// **Banda de raster que reclama canales de sprite para su fondo.**
///
/// `[top, bottom)` en líneas raster (bottom exclusivo, como el resto del engine). Los
/// canales `[channel_first, channel_first + channel_count)` quedan reservados para el
/// fondo en esa franja y no los puede usar un objeto. `attach` exige que la corrida
/// sean pares completos (canales 0+1, 2+3, 4+5, 6+7) para el fondo a 15 colores.
struct SpriteBand {
	u16 top = 0;                                             ///< primera línea de la banda (inclusive)
	u16 bottom = 0;                                          ///< línea final (exclusiva)
	SpriteBackdropTechnique technique = SpriteBackdropTechnique::None; ///< técnica de fondo
	u8 channel_first = 0;                                    ///< primer canal de la corrida (0..7)
	u8 channel_count = 0;                                    ///< canales contiguos del fondo (1..8)
	bool attach = false;                                     ///< fondo a 15 colores (pares attached)
};

/// **Ocupación canal × intervalo de líneas** (el recurso compartido por fondos y objetos).
///
/// Por canal guarda una lista corta y ordenada de intervalos `[top,bottom)` ocupados. El
/// número de intervalos por canal está acotado por el de bandas de la pantalla (pocas),
/// así que cabe en un array fijo sin heap. Los **objetos no se guardan aquí** (podrían
/// ser decenas por canal); el `SpriteAllocator` los multiplexa con su propio escalar y
/// usa el ledger solo como **pre-ocupación de los fondos**.
///
/// Invariante: los intervalos de un canal no se solapan entre sí (lo garantiza `occupy`).
class SpriteChannelLedger {
public:
	/// Canales de sprite del chipset (OCS/ECS/AGA).
	static constexpr u8 kChannels = 8;
	/// Intervalos máximos por canal: acota las bandas de fondo de una pantalla.
	static constexpr u8 kMaxIntervals = 8;

	/// Intervalo de líneas `[top,bottom)` ocupado en un canal.
	struct Interval {
		u16 top = 0;    ///< primera línea (inclusive)
		u16 bottom = 0; ///< línea final (exclusiva)
	};

	/// Vacía todas las reservas (llamar antes de planificar un frame).
	constexpr void reset() noexcept {
		for (u8 c = 0; c < kChannels; ++c) {
			m_count[c] = 0u;
		}
	}

	/// Nº de intervalos ocupados en `channel` (0 si el canal no es válido).
	[[nodiscard]] constexpr u8 interval_count(u8 channel) const noexcept {
		return channel < kChannels ? m_count[channel] : 0u;
	}

	/// Intervalo `index` del canal (vacío si fuera de rango). El orden es ascendente por `top`.
	[[nodiscard]] constexpr Interval interval(u8 channel, u8 index) const noexcept {
		return (channel < kChannels && index < m_count[channel]) ? m_iv[channel][index] : Interval {};
	}

	/// ¿Está `channel` **libre** en `[top,bottom)`? (falso si el canal es inválido o el rango vacío).
	[[nodiscard]] constexpr bool free(u8 channel, u16 top, u16 bottom) const noexcept {
		if (channel >= kChannels || top >= bottom) {
			return false;
		}
		for (u8 i = 0; i < m_count[channel]; ++i) {
			if (overlaps(m_iv[channel][i], top, bottom)) {
				return false;
			}
		}
		return true;
	}

	/// Reserva `channel` en `[top,bottom)`. `false` si solapa con otra reserva o si el canal
	/// ya está lleno de intervalos (no se pierde ninguna reserva previa en ese caso).
	constexpr bool occupy(u8 channel, u16 top, u16 bottom) noexcept {
		if (channel >= kChannels || top >= bottom || m_count[channel] >= kMaxIntervals) {
			return false;
		}
		if (!free(channel, top, bottom)) {
			return false;
		}
		// Inserción ordenada por `top` (pocos intervalos: no hay coste relevante).
		u8 i = m_count[channel];
		while (i > 0u && m_iv[channel][i - 1u].top > top) {
			m_iv[channel][i] = m_iv[channel][i - 1u];
			--i;
		}
		m_iv[channel][i] = Interval {top, bottom};
		++m_count[channel];
		return true;
	}

	/// Reserva la corrida contigua `[first, first+count)` en `[top,bottom)`. Atómico: si algún
	/// canal no está libre, no reserva ninguno.
	constexpr bool occupy_run(u8 first, u8 count, u16 top, u16 bottom) noexcept {
		if (count == 0u || first + count > kChannels) {
			return false;
		}
		for (u8 k = 0; k < count; ++k) {
			if (!free(first + k, top, bottom)) {
				return false;
			}
		}
		for (u8 k = 0; k < count; ++k) {
			(void)occupy(first + k, top, bottom);
		}
		return true;
	}

	/// Primer canal libre en `[top,bottom)`; `0xff` si ninguno.
	[[nodiscard]] constexpr u8 free_channel(u16 top, u16 bottom) const noexcept {
		for (u8 c = 0; c < kChannels; ++c) {
			if (free(c, top, bottom)) {
				return c;
			}
		}
		return 0xffu;
	}

	/// Primer canal inicial de una corrida libre de `count` en `[top,bottom)`; `0xff` si no hay.
	[[nodiscard]] constexpr u8 free_run(u8 count, u16 top, u16 bottom) const noexcept {
		if (count == 0u || count > kChannels) {
			return 0xffu;
		}
		for (u8 b = 0; b + count <= kChannels; ++b) {
			bool ok = true;
			for (u8 k = 0; k < count; ++k) {
				if (!free(b + k, top, bottom)) {
					ok = false;
					break;
				}
			}
			if (ok) {
				return b;
			}
		}
		return 0xffu;
	}

	/// Máscara de canales libres en una **línea** (bit `c` = canal `c` libre). Para telemetría
	/// y para que el juego/driver consulte cuántos canales quedan en una franja.
	[[nodiscard]] constexpr u8 free_mask(u16 line) const noexcept {
		u8 mask = 0u;
		for (u8 c = 0; c < kChannels; ++c) {
			if (free(c, line, line + 1u)) {
				mask |= (1u << c);
			}
		}
		return mask;
	}

private:
	/// ¿El intervalo `a` solapa con `[top,bottom)`? (bordes exclusivos).
	[[nodiscard]] static constexpr bool overlaps(Interval a, u16 top, u16 bottom) noexcept {
		return a.top < bottom && top < a.bottom;
	}

	/// Intervalos ocupados por canal (máx. `kMaxIntervals`, ordenados por `top`).
	Interval m_iv[kChannels][kMaxIntervals] {};
	/// Nº de intervalos usados en cada canal.
	u8 m_count[kChannels] {};
};

/// Causas de fallo de `plan_sprite_bands`.
enum class SpriteBandError : u8 {
	BadRange,     ///< `top >= bottom` (rango vacío o invertido)
	BadChannels,  ///< corrida fuera de 0..7, vacía, o attached con límites no pares
	Overlap,      ///< la banda solapa verticalmente con la anterior (deben ir ordenadas)
	ChannelBusy,  ///< el canal ya estaba ocupado en esa franja (o se agotaron los intervalos)
};

/// **Valida las bandas de fondo y las vuelca al ledger** (reparto híbrido, fase 1).
///
/// Recorre `bands` en orden (de arriba abajo) y reserva en `out` los canales de cada
/// banda con técnica distinta de `None`. Las bandas con `technique == None` se ignoran y
/// no consumen recurso. Requisitos: rangos válidos, corridas válidas, `attach` sobre
/// pares completos, y **sin solape vertical** (el fondo de una franja no se solapa con el
/// de otra; los objetos sí pueden solaparse entre franjas, eso lo resuelve el asignador).
///
/// `out` **no se resetea** aquí: el llamador decide si parte de cero (`out.reset()`) o si
/// acumula bandas de varias fuentes. Devuelve el número de bandas reservadas.
///
/// \param bands  bandas de fondo, ordenadas de arriba abajo.
/// \param out    ledger a llenar (por referencia; ver nota de reset).
/// \return nº de bandas reservadas, o el error concreto.
[[nodiscard]] inline eng::util::Expected<u8, SpriteBandError>
plan_sprite_bands(eng::Span<const SpriteBand> bands, SpriteChannelLedger& out) noexcept {
	u8 reserved = 0u;
	u16 prev_bottom = 0u;
	for (eng::usize i = 0; i < bands.size(); ++i) {
		const SpriteBand& b = bands[i];
		if (b.technique == SpriteBackdropTechnique::None) {
			continue;
		}
		if (b.top >= b.bottom) {
			return eng::util::unexpected(SpriteBandError::BadRange);
		}
		if (b.channel_count == 0u ||
		    b.channel_first + b.channel_count > SpriteChannelLedger::kChannels) {
			return eng::util::unexpected(SpriteBandError::BadChannels);
		}
		// Attached (15 colores): el fondo consume pares completos (0+1, 2+3, ...).
		if (b.attach && (((b.channel_first & 1u) != 0u) || ((b.channel_count & 1u) != 0u))) {
			return eng::util::unexpected(SpriteBandError::BadChannels);
		}
		// Bandas ordenadas de arriba abajo y sin solape vertical.
		if (reserved > 0u && b.top < prev_bottom) {
			return eng::util::unexpected(SpriteBandError::Overlap);
		}
		if (!out.occupy_run(b.channel_first, b.channel_count, b.top, b.bottom)) {
			return eng::util::unexpected(SpriteBandError::ChannelBusy);
		}
		prev_bottom = b.bottom;
		++reserved;
	}
	return reserved;
}

} // namespace eng::graphics
