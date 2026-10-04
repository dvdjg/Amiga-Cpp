#pragma once

/// \file sprite_channel_window.hpp
/// **Reparto de los 8 canales de sprite por ventanas de reprogramación**.
///
/// En el chipset OCS/ECS/AGA **no hay "bandas" de hardware**: cada uno de los 8 canales es
/// una secuencia DMA que el Copper puede **reprogramar en cualquier línea** (rearmar el
/// puntero, cambiar `POS`/`CTL`/`DATA`). Lo que aquí se agrupa como **ventana**
/// (`SpriteChannelWindow`) es solo la convención de un intervalo `[top,bottom)` durante el
/// cual una **corrida de canales** se reprograma de forma semejante para un **fondo** por
/// sprites (`Layer`, `RiskyWoods`, `FreeForm`). Varias ventanas pueden **solaparse en
/// vertical** mientras usen **canales distintos**: el único límite real es *por canal* (dos
/// fondos no pueden reusar el mismo canal en líneas solapadas).
///
///   - `SpriteChannelWindow`: ventana `[top,bottom)` + corrida de canales + técnica de fondo.
///   - `SpriteChannelLedger`: ocupación **canal × intervalo de líneas**. Es el recurso real:
///     los fondos la llenan con `occupy_run`; el `SpriteAllocator` la consulta para repartir
///     los **objetos** solo en los canales libres de su intervalo.
///   - `plan_sprite_windows`: valida las ventanas y las vuelca al ledger.
///
/// Ejemplo (el caso híbrido que motiva el diseño): un fondo *Risky Woods* reprograma 6
/// canales en `[60,100)` y deja los canales 6 y 7 libres para objetos en ese intervalo;
/// fuera de él, los 8 canales vuelven a estar disponibles:
///
/// ```text
///   canal 0..5  ├──── fondo Risky Woods [60,100) ────┤
///   canal 6..7  (libres)  ← 2 canales para objetos en el intervalo
/// ```
///
/// Es lógica **pura** (sin hardware, sin heap, sin STL): host-testable. El driver de cada
/// técnica de fondo (`effects::SpriteLayer`/`effects::RiskyWoodsLayer`, `SpriteLineLayer`) es
/// quien emite los MOVEs de Copper; aquí solo se reserva y se consulta el recurso.
/// Diseño completo: `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/expected.hpp>
#include <eng/graphics/sprite_limits.hpp>

namespace eng::graphics {

/// **Técnica de fondo por sprites** de una ventana.
///
/// Es una etiqueta de intención para el planner y los drivers; el ledger solo usa la
/// cuenta de canales y el rango de líneas. La técnica determina cómo el driver emite
/// la copperlist del intervalo (ver `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`).
enum class SpriteBackdropTechnique : u8 {
	None = 0,    ///< la ventana no usa sprites para el fondo (no reserva canales)
	Layer,       ///< N canales contiguos, un patrón de 16 px por canal (`effects::SpriteLayer`)
	RiskyWoods,  ///< reposición horizontal repetida: patrón de 64 px (15 colores con attached)
	FreeForm,    ///< reposición + DATA distinta por columna: fondo libre, sin patrón repetido
};

/// **Ventana de reprogramación: un intervalo `[top,bottom)` y una corrida de canales.**
///
/// `[top, bottom)` en líneas raster (bottom exclusivo). **No** implica ninguna partición de
/// la pantalla: es la agrupación de los canales que se reprograman para un fondo durante ese
/// intervalo. Los canales `[channel_first, channel_first + channel_count)` quedan reservados
/// en el ledger para ese fondo y no los puede usar ni un objeto ni otro fondo en esas líneas
/// (dos ventanas pueden solaparse en vertical si usan canales distintos). `attach` exige que
/// la corrida sean pares completos (canales 0+1, 2+3, 4+5, 6+7) para el fondo a 15 colores.
struct SpriteChannelWindow {
	u16 top = 0;                                             ///< primera línea de la ventana (inclusive)
	u16 bottom = 0;                                          ///< línea final (exclusiva)
	SpriteBackdropTechnique technique = SpriteBackdropTechnique::None; ///< técnica de fondo
	u8 channel_first = 0;                                    ///< primer canal de la corrida (0..7)
	u8 channel_count = 0;                                    ///< canales contiguos del fondo (1..8)
	bool attach = false;                                     ///< fondo a 15 colores (pares attached)
};

/// **Ocupación canal × intervalo de líneas** (el recurso compartido por fondos y objetos).
///
/// Por canal guarda una lista corta y ordenada de intervalos `[top,bottom)` ocupados. El
/// número de intervalos por canal está acotado por el de ventanas de la pantalla (pocas),
/// así que cabe en un array fijo sin heap. Los **objetos no se guardan aquí** (podrían
/// ser decenas por canal); el `SpriteAllocator` los multiplexa con su propio escalar y
/// usa el ledger solo como **pre-ocupación de los fondos**.
///
/// Invariante: los intervalos de un canal no se solapan entre sí (lo garantiza `occupy`).
class SpriteChannelLedger {
public:
	/// Canales de sprite del chipset (OCS/ECS/AGA); fuente única: `sprite_limits.hpp`.
	static constexpr u8 kChannels = kSpriteChannels;
	/// Intervalos máximos por canal: acota las ventanas de fondo de una pantalla.
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
	/// y para que el juego/driver consulte cuántos canales quedan en un intervalo.
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

/// Causas de fallo de `plan_sprite_windows`.
enum class SpriteChannelWindowError : u8 {
	BadRange,     ///< `top >= bottom` (rango vacío o invertido)
	BadChannels,  ///< corrida fuera de 0..7, vacía, o attached con límites no pares
	ChannelBusy,  ///< el canal ya estaba ocupado en ese intervalo (o se agotaron los intervalos)
};

/// **Valida las ventanas de fondo y las vuelca al ledger** (reparto híbrido, fase 1).
///
/// Recorre `windows` y reserva en `out` los canales de cada ventana con técnica distinta de
/// `None`. Las ventanas con `technique == None` se ignoran y no consumen recurso. Requisitos:
/// rangos válidos, corridas válidas y `attach` sobre pares completos. **No** se exige que las
/// ventanas sean disjuntas en vertical: dos ventanas pueden solaparse en líneas si usan
/// canales distintos; si comparten canal en líneas solapadas, la reserva falla con
/// `ChannelBusy` (el único conflicto real, porque cada canal se reprograma de forma
/// independiente en cualquier línea).
///
/// `out` **no se resetea** aquí: el llamador decide si parte de cero (`out.reset()`) o si
/// acumula ventanas de varias fuentes. Devuelve el número de ventanas reservadas.
///
/// \param windows  ventanas de fondo (el orden no importa; ver nota de solape).
/// \param out      ledger a llenar (por referencia; ver nota de reset).
/// \return nº de ventanas reservadas, o el error concreto.
[[nodiscard]] inline eng::util::Expected<u8, SpriteChannelWindowError>
plan_sprite_windows(eng::Span<const SpriteChannelWindow> windows, SpriteChannelLedger& out) noexcept {
	u8 reserved = 0u;
	for (eng::usize i = 0; i < windows.size(); ++i) {
		const SpriteChannelWindow& w = windows[i];
		if (w.technique == SpriteBackdropTechnique::None) {
			continue;
		}
		if (w.top >= w.bottom) {
			return eng::util::unexpected(SpriteChannelWindowError::BadRange);
		}
		if (w.channel_count == 0u ||
		    w.channel_first + w.channel_count > SpriteChannelLedger::kChannels) {
			return eng::util::unexpected(SpriteChannelWindowError::BadChannels);
		}
		// Attached (15 colores): el fondo consume pares completos (0+1, 2+3, ...).
		if (w.attach && (((w.channel_first & 1u) != 0u) || ((w.channel_count & 1u) != 0u))) {
			return eng::util::unexpected(SpriteChannelWindowError::BadChannels);
		}
		if (!out.occupy_run(w.channel_first, w.channel_count, w.top, w.bottom)) {
			return eng::util::unexpected(SpriteChannelWindowError::ChannelBusy);
		}
		++reserved;
	}
	return reserved;
}

} // namespace eng::graphics
