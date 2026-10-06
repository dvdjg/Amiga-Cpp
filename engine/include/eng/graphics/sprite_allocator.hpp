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
/// ese intervalo, marca `as_bob`. Es el mismo criterio que usan los juegos reales
/// (Turrican, etc.) para repartir objetos entre sprites y blits.
///
/// **Reparto híbrido:** el overload con `reserved` (`SpriteChannelLedger`) descuenta
/// los canales que un **fondo por sprites** ocupa en un intervalo (`SpriteChannelWindow`), de
/// modo que los objetos usan solo los canales libres de ese intervalo y los recuperan por
/// encima y por debajo. Es la base de mezclar técnicas por ventana (Risky Woods, Free Form)
/// con objetos tradicionales. Ver `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`.
///
/// Es lógica pura (sin hardware, sin heap), host-testable.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_channel_window.hpp>

namespace eng::graphics {

/// Resultado de asignar un sprite a un canal hardware.
struct SpriteSlot {
	u8  channel = 0;     // canal asignado (0..7)
	bool as_bob = false; // true = no cabe en sprites, debe dibujarse como BOB
};

/// Asignador de canales con multiplexado vertical (greedy first-fit).
///
/// Contrato: `intents` debe venir ordenado por `top` ascendente (el llamador lo
/// garantiza; no hay heap para ordenar). Un canal queda libre cuando su último uso
/// terminó en `<= top` (`bottom` es exclusivo), de modo que dos sprites contiguos
/// comparten canal y uno que empieza en la línea 0 también encuentra canal. El
/// `channel` de cada `SpriteIntent` se trata como preferencia y se ignora en esta
/// versión mínima (el asignador decide). **Sí** modela los pares **attached** (15 colores):
/// el par va en un canal PAR y el intent impar (`attach = true`) en el contiguo. El ancho
/// de 32 px de AGA (`width_words = 2`) no cambia el nº de canales (1 por sprite), solo el
/// coste de DMA; no altera la asignación.
///
/// También modela las **cadenas verticales** (`chain_id`/`chain_index`/`chain_span`): las
/// franjas de un mismo objeto ("chasing the raster") van al MISMO canal, que el líder
/// reserva para el rango completo de la cadena; si el líder no cabe, la cadena entera
/// degrada a BOB. Ver `sprite_template_to_intents`.
class SpriteAllocator {
public:
	/// Canales de sprite del chipset; fuente única: `sprite_limits.hpp`.
	static constexpr u8 kChannels = kSpriteChannels;

	constexpr SpriteAllocator() = default;

	/// Asigna `count` intents. Escribe el canal (o `as_bob`) de cada uno en `out`.
	///
	/// Devuelve cuántos caben en hardware (los restantes quedan `as_bob`), para
	/// telemetría: `bobs = count - result`.
	///
	/// `reserved` es el **ledger de fondos por intervalo** (`SpriteChannelLedger`): un canal
	/// ocupado por el fondo de una ventana no se ofrece a un objeto en ese intervalo, pero sí
	/// por encima o por debajo (multiplexado vertical). Con `reserved` nulo/vacío el
	/// reparto es el clásico de 8 canales. Ver `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`.
	///
	/// Las **tiras horizontales** (`strip_span > 1`) reservan una corrida de canales
	/// contiguos: si no hay una corrida libre del tamaño pedido, la tira ENTERA va a
	/// `as_bob` (no se parte a medias).
	u8 assign(eng::Span<const SpriteIntent> intents, eng::Span<SpriteSlot> out,
		  eng::Ref<const SpriteChannelLedger> reserved = {}) {
		const u8 count = static_cast<u8>(intents.size());
		u16 busy_until[kChannels] {};
		u8 in_hardware = 0;
		u8 run_id = 0;   // tira en curso (0 = ninguna)
		u8 run_base = 0; // primer canal de la corrida reservada
		u8 run_span = 1;
		bool run_ok = false;
		// Cadenas verticales en curso: id -> canal reservado hasta `end`.
		struct ChainState {
			u8 id = 0u;
			u8 channel = 0u;
			u16 end = 0u;
		};
		ChainState chains[kChannels] {};
		// Canal libre para un objeto en `[top,bottom)`: ningún objeto anterior lo ocupa
		// (`busy_until <= top`) y el fondo no lo reserva (`ledger.free`).
		auto ch_free = [&](u8 c, u16 top, u16 bottom) -> bool {
			if (busy_until[c] > top) {
				return false;
			}
			return !reserved.valid() || reserved->free(c, top, bottom);
		};
		for (u8 i = 0; i < count; ++i) {
			const SpriteIntent& it = intents[i];
			// Retira las cadenas cuyo último tramo ya pasó (liberan su entrada).
			for (u8 k = 0; k < kChannels; ++k) {
				if (chains[k].id != 0u && chains[k].end <= it.top) {
					chains[k].id = 0u;
				}
			}
			// **Cadena vertical** (mismo objeto en franjas, "chasing the raster"): el
			// líder reserva un canal para TODO el rango de la cadena y los seguidores lo
			// reutilizan. Si el líder no cabe, toda la cadena degrada.
			if (it.chain_id != 0u && it.chain_span > 1u) {
				u8 entry = 0xff;
				for (u8 k = 0; k < kChannels; ++k) {
					if (chains[k].id == it.chain_id) {
						entry = k;
						break;
					}
				}
				if (entry != 0xff) {
					out[i] = SpriteSlot {chains[entry].channel, false};
					++in_hardware;
					continue;
				}
				if (it.chain_index != 0u) {
					out[i] = SpriteSlot {0u, true}; // seguidor sin líder: degrada
					continue;
				}
				// Fin de la cadena = mayor `bottom` de sus miembros.
				u16 end = it.bottom;
				u8 found = 1u;
				for (u8 k = static_cast<u8>(i + 1u); k < count && found < it.chain_span; ++k) {
					if (intents[k].chain_id != it.chain_id) {
						continue;
					}
					if (intents[k].bottom > end) {
						end = intents[k].bottom;
					}
					++found;
				}
				u8 channel = 0xff;
				for (u8 c = 0; c < kChannels; ++c) {
					if (ch_free(c, it.top, end)) {
						channel = c;
						break;
					}
				}
				u8 slot = 0xff;
				if (channel != 0xff) {
					for (u8 k = 0; k < kChannels; ++k) {
						if (chains[k].id == 0u) {
							slot = k;
							break;
						}
					}
				}
				if (channel == 0xff || slot == 0xff) {
					out[i] = SpriteSlot {0u, true};
					continue;
				}
				chains[slot] = ChainState {it.chain_id, channel, end};
				busy_until[channel] = end;
				out[i] = SpriteSlot {channel, false};
				++in_hardware;
				continue;
			}
			if (it.strip_span > 1u && it.strip_id != 0u) {
				if (it.strip_id != run_id) {
					// Líder de una tira nueva: solo si llega con el índice 0 (el
					// tramo de la izquierda) se busca corrida; si no, se rechaza.
					run_id = it.strip_id;
					run_span = it.strip_span;
					run_base = 0;
					run_ok = false;
					if (it.strip_index == 0u) {
						for (u8 b = 0; static_cast<u16>(b) + run_span <= kChannels; ++b) {
							bool free_run = true;
							for (u8 k = 0; k < run_span; ++k) {
								if (!ch_free(static_cast<u8>(b + k), it.top, it.bottom)) {
									free_run = false;
									break;
								}
							}
							if (free_run) {
								run_base = b;
								run_ok = true;
								break;
							}
						}
						if (run_ok) {
							for (u8 k = 0; k < run_span; ++k) {
								busy_until[static_cast<u8>(run_base + k)] = it.bottom;
							}
						}
					}
				}
				if (run_ok && it.strip_id == run_id && it.strip_index < run_span) {
					out[i] = SpriteSlot {static_cast<u8>(run_base + it.strip_index), false};
					++in_hardware;
				} else {
					out[i] = SpriteSlot {0u, true}; // la tira no cabe entera
				}
				continue;
			}
			// Attached (15 colores): el par va en un canal PAR (0+1, 2+3, ...) y el
			// siguiente intent (el impar, `attach = true`) ocupa el canal contiguo.
			const bool pair_leader =
				(static_cast<u8>(i + 1u) < count) && intents[i + 1u].attach;
			if (pair_leader) {
				u8 even = 0xff;
				for (u8 c = 0; static_cast<u8>(c + 1u) < kChannels;
				     c = static_cast<u8>(c + 2u)) {
					if (ch_free(c, it.top, it.bottom) &&
					    ch_free(c + 1u, it.top, it.bottom)) {
						even = c;
						break;
					}
				}
				if (even == 0xff) {
					out[i] = SpriteSlot {0u, true};
				} else {
					out[i] = SpriteSlot {even, false};
					busy_until[even] = it.bottom;
					++in_hardware;
				}
				continue;
			}
			if (it.attach) {
				if (i > 0u && !out[i - 1u].as_bob) {
					const u8 even = out[i - 1u].channel;
					const u8 odd = static_cast<u8>(even + 1u);
					if ((even % 2u) == 0u && odd < kChannels &&
					    ch_free(odd, it.top, it.bottom)) {
						out[i] = SpriteSlot {odd, false};
						busy_until[odd] = it.bottom;
						++in_hardware;
						continue;
					}
				}
				out[i] = SpriteSlot {0u, true}; // el par no cabe: el impar va a BOB
				continue;
			}
			u8 channel = 0xff;
			for (u8 c = 0; c < kChannels; ++c) {
				if (ch_free(c, it.top, it.bottom)) {
					channel = c;
					break;
				}
			}
			if (channel == 0xff) {
				out[i] = SpriteSlot {0, true};
			} else {
				out[i] = SpriteSlot {channel, false};
				busy_until[channel] = it.bottom;
				++in_hardware;
			}
		}
		return in_hardware;
	}
};

} // namespace eng::graphics
