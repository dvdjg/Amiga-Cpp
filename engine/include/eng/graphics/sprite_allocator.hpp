#pragma once

/// \file sprite_allocator.hpp
/// Asignador de canales de sprite hardware (paso 4 de ENGINE_DESIGN.md §5).
///
/// Decide, sin tocar hardware, QUÉ canal de los 8 usa cada `SpriteIntent` y
/// cuándo un sprite no cabe y debe degradarse a BOB (la transición
/// "sprite → BOB transparente" del engine). Es la pieza que separa el "qué quiero
/// mostrar" (`SpriteIntent`/`Visual`) del "cómo se materializa" (`SpriteManager`).
///
/// Algoritmo: *greedy first-fit* con multiplexado vertical. Procesa los intents
/// ordenados por `top` ascendente y asigna a cada uno el primer canal cuyo
/// `busy_until < top` (sin solape vertical). Si los 8 canales están ocupados en
/// esa franja, marca `as_bob`. Es el mismo criterio que usan los juegos reales
/// (Turrican, etc.) para repartir objetos entre sprites y blits.
///
/// Es lógica pura (sin hardware, sin heap), host-testable.

#include <eng/core/types.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics {

/// Resultado de asignar un sprite a un canal hardware.
struct SpriteSlot {
	u8  channel = 0;     // canal asignado (0..7)
	bool as_bob = false; // true = no cabe en sprites, debe dibujarse como BOB
};

/// Asignador de canales con multiplexado vertical (greedy first-fit).
///
/// Contrato: `intents` debe venir ordenado por `top` ascendente (el llamador lo
/// garantiza; no hay heap para ordenar). El `channel` de cada `SpriteIntent` se
/// trata como preferencia y se ignora en esta versión mínima (el asignador
/// decide). No modela aún los pares attached (15 colores, 2 canales) ni el ancho
/// de 32 px de AGA; se asume 16 px no attached por defecto.
class SpriteAllocator {
public:
	static constexpr u8 kChannels = 8;

	constexpr SpriteAllocator() = default;

	/// Asigna `count` intents. Escribe el canal (o `as_bob`) de cada uno en `out`.
	///
	/// Devuelve cuántos caben en hardware (los restantes quedan `as_bob`), para
	/// telemetría: `bobs = count - result`.
	u8 assign(const SpriteIntent* intents, u8 count, SpriteSlot* out) {
		u16 busy_until[kChannels] {};
		u8 in_hardware = 0;
		for (u8 i = 0; i < count; ++i) {
			const SpriteIntent& it = intents[i];
			u8 channel = 0xff;
			for (u8 c = 0; c < kChannels; ++c) {
				if (busy_until[c] < it.top) {
					channel = c;
					break;
				}
			}
			if (channel == 0xff) {
				out[i] = SpriteSlot{0, true};
			} else {
				out[i] = SpriteSlot{channel, false};
				busy_until[channel] = it.bottom;
				++in_hardware;
			}
		}
		return in_hardware;
	}
};

} // namespace eng::graphics
