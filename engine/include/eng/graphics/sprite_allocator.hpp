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
/// ordenados por `top` ascendente y asigna a cada uno el primer canal libre en su
/// intervalo. Si los 8 canales están ocupados, marca `as_bob`. Es el mismo criterio que
/// usan los juegos reales (Turrican, etc.) para repartir objetos entre sprites y blits.
///
/// **Reparto híbrido:** el overload con `reserved` (`SpriteChannelLedger`) descuenta
/// los canales que un **fondo por sprites** ocupa en un intervalo (`SpriteChannelWindow`), de
/// modo que los objetos usan solo los canales libres de ese intervalo y los recuperan por
/// encima y por debajo. Es la base de mezclar técnicas por ventana (Risky Woods, Free Form)
/// con objetos tradicionales. Ver `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`.
///
/// **Ocupación exacta por línea** (bitfield de 256 líneas por canal, arrays fijos sin heap):
/// un canal está libre en `[top,bottom)` si ninguna línea de ese tramo está ocupada. Sustituye
/// a un simple `lastY[8]`, que basta cuando los intents llegan ordenados por Y; el bitfield
/// además permite que las **pasadas por prioridad** (abajo) no pierdan reutilización: un
/// objeto libre por delante de un fijo en Y puede reusar el canal del fijo si sus tramos no
/// se solapan.
///
/// **Preferencia de canal:** el `channel` del intent es una **preferencia**: el allocator lo
/// intenta primero (canal par para pares *attached*, base de la corrida para tiras/grupos) y
/// si está ocupado reparte el primero libre. Es la "tabla de slots preferidos" del
/// multiplexor clásico (`sprite-multiplexer-bob-fallback.md` §2).
///
/// **Prioridad de asignación:** `SpriteIntent::assign_rank` (0 = normal, 1..3 = antes).
/// El allocator recorre los intents una vez por rango, de mayor a menor: los **fijos y los
/// grupos** eligen canal antes que los libres, conservando su `channel` preferido aunque un
/// objeto libre tenga un `top` menor.
///
/// **Grupos con trayectoria** (`group_id`/`group_index`/`group_span`): un grupo reclama una
/// **corrida de canales contiguos** para el **bounding box** de todos sus miembros y cada uno
/// ocupa `base + group_index`; si la corrida no cabe, el grupo **entero** degrada a BOB
/// (dibujo coherente, `sprite-multiplexer-bob-fallback.md` §4). Es el análogo vertical de las
/// tiras horizontales.
///
/// Es lógica pura (sin hardware, sin heap), host-testable.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/bitset.hpp>
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
/// garantiza; no hay heap para ordenar). Con `assign_rank` el orden dentro de cada rango se
/// conserva. El `channel` de cada `SpriteIntent` es preferencia (ver arriba). **Sí** modela
/// los pares **attached** (15 colores): el par va en un canal PAR y el intent impar
/// (`attach = true`) en el contiguo. El ancho de 32 px de AGA (`width_words = 2`) no cambia
/// el nº de canales (1 por sprite), solo el coste de DMA; no altera la asignación.
///
/// También modela las **cadenas verticales** (`chain_id`/`chain_index`/`chain_span`): las
/// franjas de un mismo objeto ("chasing the raster") van al MISMO canal, que el líder
/// reserva para el rango completo de la cadena; si el líder no cabe, la cadena entera
/// degrada a BOB. Ver `sprite_template_to_intents`.
class SpriteAllocator {
public:
	/// Canales de sprite del chipset; fuente única: `sprite_limits.hpp`.
	static constexpr u8 kChannels = kSpriteChannels;
	/// Rango máximo de asignación (0 = normal; 1..3 = se asigna antes).
	static constexpr u8 kMaxAssignRank = 3u;
	/// Líneas cubiertas por el bitfield de ocupación (256 visibles); por encima no se
	/// rastrea (borde inferior).
	static constexpr u16 kOccupancyLines = 256u;
	static constexpr u16 kOccupancyWords = kOccupancyLines / 16u; // 16 words = 256 bits

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
		if (out.size() < count) {
			for (eng::usize i = 0; i < out.size(); ++i) {
				out[i] = SpriteSlot {0u, true};
			}
			return 0u;
		}
		for (u8 i = 0; i < count; ++i) {
			out[i] = SpriteSlot {0u, true}; // por defecto: BOB
		}
		u8 in_hardware = 0;
		eng::util::BitSet<256> done {}; // un intent se asigna una sola vez (varias pasadas)

		// Ocupación exacta por línea (bit por línea visible) de cada canal.
		u16 occ[kChannels][kOccupancyWords] {};
		// Estado de las corridas verticales en curso.
		struct GroupState {
			u8 id = 0u;
			u8 base = 0u;
			u16 end = 0u;
		};
		GroupState groups[kChannels] {};
		struct ChainState {
			u8 id = 0u;
			u8 channel = 0u;
			u16 end = 0u;
		};
		ChainState chains[kChannels] {};
		// Tira horizontal en curso (los miembros llegan consecutivos).
		u8 run_id = 0u;
		u8 run_base = 0u;
		u8 run_span = 1u;
		bool run_ok = false;

		// Canal libre para un tramo `[top,bottom)`: ninguna línea ocupada y el fondo no lo
		// reserva (`ledger.free`).
		auto ch_free = [&](u8 c, u16 top, u16 bottom) -> bool {
			if (!lines_free(occ[c], top, bottom)) {
				return false;
			}
			return !reserved.valid() || reserved->free(c, top, bottom);
		};
		auto reserve = [&](u8 c, u16 top, u16 bottom) { mark_lines(occ[c], top, bottom); };
		// Retira las corridas cuyo último tramo ya pasó (liberan su entrada).
		auto retire = [&](u16 top) {
			for (u8 k = 0; k < kChannels; ++k) {
				if (groups[k].id != 0u && groups[k].end <= top) {
					groups[k].id = 0u;
				}
				if (chains[k].id != 0u && chains[k].end <= top) {
					chains[k].id = 0u;
				}
			}
		};

		// Pasadas por rango de asignación (mayor primero): fijos y grupos eligen canal
		// antes que los libres. Dentro de cada pasada se conserva el orden por `top`.
		for (u8 pass = kMaxAssignRank;; --pass) {
			for (u8 i = 0; i < count; ++i) {
				if (done.test(i)) {
					continue;
				}
				const SpriteIntent& it = intents[i];
				const u8 rank = it.assign_rank > kMaxAssignRank ? kMaxAssignRank
										: it.assign_rank;
				if (rank != pass) {
					continue;
				}
				retire(it.top);
				done.set(i);

				// **Grupo con trayectoria**: corrida contigua para el bounding box.
				if (it.group_id != 0u && it.group_span > 1u) {
					u8 entry = 0xff;
					for (u8 k = 0; k < kChannels; ++k) {
						if (groups[k].id == it.group_id) {
							entry = k;
							break;
						}
					}
					if (entry != 0xff) {
						// Miembro: su canal es base + índice (si cabe en la corrida).
						if (it.group_index < it.group_span &&
						    static_cast<u16>(groups[entry].base) + it.group_index <
							    kChannels) {
							out[i] = SpriteSlot {
								static_cast<u8>(groups[entry].base + it.group_index),
								false};
							++in_hardware;
						}
						continue;
					}
					if (it.group_index != 0u) {
						continue; // miembro sin líder: as_bob
					}
					// Líder: bounding box de todos los miembros.
					u16 gtop = it.top;
					u16 gbot = it.bottom;
					for (u8 k = 0; k < count; ++k) {
						if (intents[k].group_id != it.group_id) {
							continue;
						}
						if (intents[k].top < gtop) {
							gtop = intents[k].top;
						}
						if (intents[k].bottom > gbot) {
							gbot = intents[k].bottom;
						}
					}
					u8 base = 0xff;
					// Preferido primero (si la corrida entera cabe desde ahí).
					if (it.channel < kChannels &&
					    static_cast<u16>(it.channel) + it.group_span <= kChannels) {
						bool ok = true;
						for (u8 c = 0; c < it.group_span; ++c) {
							if (!ch_free(static_cast<u8>(it.channel + c), gtop,
								     gbot)) {
								ok = false;
								break;
							}
						}
						if (ok) {
							base = it.channel;
						}
					}
					if (base == 0xff) {
						for (u8 b = 0;
						     static_cast<u16>(b) + it.group_span <= kChannels; ++b) {
							bool ok = true;
							for (u8 c = 0; c < it.group_span; ++c) {
								if (!ch_free(static_cast<u8>(b + c), gtop,
									     gbot)) {
									ok = false;
									break;
								}
							}
							if (ok) {
								base = b;
								break;
							}
						}
					}
					u8 slot = 0xff;
					if (base != 0xff) {
						for (u8 k = 0; k < kChannels; ++k) {
							if (groups[k].id == 0u) {
								slot = k;
								break;
							}
						}
					}
					if (base == 0xff || slot == 0xff) {
						continue; // el grupo entero degrada a BOB
					}
					groups[slot] = GroupState {it.group_id, base, gbot};
					for (u8 c = 0; c < it.group_span; ++c) {
						reserve(static_cast<u8>(base + c), gtop, gbot);
					}
					out[i] = SpriteSlot {base, false};
					++in_hardware;
					continue;
				}

				// **Tira horizontal** de canales contiguos.
				if (it.strip_span > 1u && it.strip_id != 0u) {
					if (it.strip_id != run_id) {
						// Líder de una tira nueva: solo si llega con el índice 0 (el
						// tramo de la izquierda) se busca corrida; si no, se rechaza.
						run_id = it.strip_id;
						run_span = it.strip_span;
						run_base = 0u;
						run_ok = false;
						if (it.strip_index == 0u) {
							if (it.channel < kChannels &&
							    static_cast<u16>(it.channel) + run_span <=
								    kChannels) {
								bool ok = true;
								for (u8 k = 0; k < run_span; ++k) {
									if (!ch_free(
										    static_cast<u8>(it.channel + k),
										    it.top, it.bottom)) {
										ok = false;
										break;
									}
								}
								if (ok) {
									run_base = it.channel;
									run_ok = true;
								}
							}
							for (u8 b = 0;
							     !run_ok &&
							     static_cast<u16>(b) + run_span <= kChannels;
							     ++b) {
								bool free_run = true;
								for (u8 k = 0; k < run_span; ++k) {
									if (!ch_free(static_cast<u8>(b + k),
										     it.top, it.bottom)) {
										free_run = false;
										break;
									}
								}
								if (free_run) {
									run_base = b;
									run_ok = true;
								}
							}
							if (run_ok) {
								for (u8 k = 0; k < run_span; ++k) {
									reserve(static_cast<u8>(run_base + k),
										it.top, it.bottom);
								}
							}
						}
					}
					if (run_ok && it.strip_id == run_id && it.strip_index < run_span) {
						out[i] = SpriteSlot {
							static_cast<u8>(run_base + it.strip_index), false};
						++in_hardware;
					}
					continue;
				}

				// **Attached (15 colores)**: el par va en un canal PAR (0+1, 2+3, ...) y
				// el siguiente intent (el impar, `attach = true`) ocupa el contiguo.
				const bool pair_leader =
					(static_cast<u8>(i + 1u) < count) && intents[i + 1u].attach;
				if (pair_leader) {
					u8 even = 0xff;
					// Preferido (par) primero.
					if ((it.channel % 2u) == 0u &&
					    static_cast<u8>(it.channel + 1u) < kChannels &&
					    ch_free(it.channel, it.top, it.bottom) &&
					    ch_free(static_cast<u8>(it.channel + 1u), it.top, it.bottom)) {
						even = it.channel;
					}
					for (u8 c = 0; even == 0xff && static_cast<u8>(c + 1u) < kChannels;
					     c = static_cast<u8>(c + 2u)) {
						if (ch_free(c, it.top, it.bottom) &&
						    ch_free(static_cast<u8>(c + 1u), it.top, it.bottom)) {
							even = c;
							break;
						}
					}
					if (even == 0xff) {
						continue; // out[i] queda as_bob
					}
					out[i] = SpriteSlot {even, false};
					reserve(even, it.top, it.bottom); // el impar lo reserva el seguidor
					++in_hardware;
					continue;
				}
				if (it.attach) {
					if (i > 0u && !out[i - 1u].as_bob) {
						const u8 even = out[i - 1u].channel;
						const u8 odd = static_cast<u8>(even + 1u);
						if ((even % 2u) == 0u && odd < kChannels &&
						    ch_free(odd, it.top, it.bottom)) {
							out[i] = SpriteSlot {odd, false};
							reserve(odd, it.top, it.bottom);
							++in_hardware;
							continue;
						}
					}
					continue; // el par no cabe: el impar va a BOB
				}

				// **Cadena vertical** (mismo objeto en franjas): el líder reserva un canal
				// para TODO el rango de la cadena y los seguidores lo reutilizan.
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
						continue; // seguidor sin líder: as_bob
					}
					// Fin de la cadena = mayor `bottom` de sus miembros.
					u16 end = it.bottom;
					u8 found = 1u;
					for (u8 k = static_cast<u8>(i + 1u);
					     k < count && found < it.chain_span; ++k) {
						if (intents[k].chain_id != it.chain_id) {
							continue;
						}
						if (intents[k].bottom > end) {
							end = intents[k].bottom;
						}
						++found;
					}
					u8 channel = 0xff;
					if (it.channel < kChannels && ch_free(it.channel, it.top, end)) {
						channel = it.channel;
					}
					for (u8 c = 0; channel == 0xff && c < kChannels; ++c) {
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
						continue; // la cadena entera degrada a BOB
					}
					chains[slot] = ChainState {it.chain_id, channel, end};
					reserve(channel, it.top, end);
					out[i] = SpriteSlot {channel, false};
					++in_hardware;
					continue;
				}

				// **Suelto**: preferido primero, después first-fit.
				u8 channel = 0xff;
				if (it.channel < kChannels && ch_free(it.channel, it.top, it.bottom)) {
					channel = it.channel;
				}
				for (u8 c = 0; channel == 0xff && c < kChannels; ++c) {
					if (ch_free(c, it.top, it.bottom)) {
						channel = c;
						break;
					}
				}
				if (channel == 0xff) {
					continue; // as_bob
				}
				out[i] = SpriteSlot {channel, false};
				reserve(channel, it.top, it.bottom);
				++in_hardware;
			}
			if (pass == 0u) {
				break;
			}
		}
		return in_hardware;
	}

private:
	/// ¿Ninguna línea de `[top,bottom)` ocupada? Las líneas por encima de 255 no se
	/// rastrean (borde inferior) y `bottom` se recorta a 256.
	[[nodiscard]] static bool lines_free(const u16* occ, u16 top, u16 bottom) noexcept {
		if (top >= kOccupancyLines || bottom <= top) {
			return true;
		}
		if (bottom > kOccupancyLines) {
			bottom = kOccupancyLines;
		}
		const u16 w0 = static_cast<u16>(top >> 4u);
		const u16 w1 = static_cast<u16>((bottom - 1u) >> 4u);
		for (u16 w = w0; w <= w1; ++w) {
			u16 mask = 0xffffu;
			if (w == w0) {
				mask = static_cast<u16>(mask << (top & 15u));
			}
			if (w == w1) {
				mask = static_cast<u16>(
					mask & static_cast<u16>(0xffffu >> (15u - ((bottom - 1u) & 15u))));
			}
			if ((occ[w] & mask) != 0u) {
				return false;
			}
		}
		return true;
	}

	/// Marca `[top,bottom)` como ocupado (mismas reglas de recorte que `lines_free`).
	static void mark_lines(u16* occ, u16 top, u16 bottom) noexcept {
		if (top >= kOccupancyLines || bottom <= top) {
			return;
		}
		if (bottom > kOccupancyLines) {
			bottom = kOccupancyLines;
		}
		const u16 w0 = static_cast<u16>(top >> 4u);
		const u16 w1 = static_cast<u16>((bottom - 1u) >> 4u);
		for (u16 w = w0; w <= w1; ++w) {
			u16 mask = 0xffffu;
			if (w == w0) {
				mask = static_cast<u16>(mask << (top & 15u));
			}
			if (w == w1) {
				mask = static_cast<u16>(
					mask & static_cast<u16>(0xffffu >> (15u - ((bottom - 1u) & 15u))));
			}
			occ[w] = static_cast<u16>(occ[w] | mask);
		}
	}
};

} // namespace eng::graphics
