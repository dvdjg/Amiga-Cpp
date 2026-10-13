#pragma once

/// \file sprite_chain.hpp
/// **Emisión DMA encadenada por canal** (F3 de `ROADMAP_SPRITES_COMPLETO.md` §3.2):
/// construye, en Chip RAM, la estructura que Agnus recorre sola para servir **varios
/// placements en líneas disjuntas** por un mismo canal:
///
/// ```text
///   SPRxPT ──► [POS,CTL,DAT/DATB…][POS,CTL,DAT/DATB…]…[0,0]
///              └ bloque 1 (franja A)  └ bloque 2 (franja B)  terminador
/// ```
///
/// Es la alternativa al rearme por Copper (`SpriteManager::emit_placements_into`: un
/// `WAIT` + 4 MOVEs por reaparición): la **primera** franja del canal se arma por
/// registro (`SPRxPOS/CTL` + `SPRxPT` a la DATA de su bloque, patrón validado de
/// `arm_object`) y el walk del DMA consume los bloques siguientes solo.
///
/// **Mecanismo (fuente del emulador; AGENTS §0 y §1.12).** Con `dmastate == 0` el DMA lee
/// `POS` y `CTL` de la memoria apuntada por `SPRxPT` y los procesa como cabecera
/// (`../WinUAE-DBG/custom.cpp:4078-4090`, `SPRxCTL_DMA`, que además fuerza `dmastate = 0`
/// al decodificarla); `sprstartstop` arranca el canal en `vpos == VSTART` y lo para en
/// `vpos == VSTOP` (`custom.cpp:4031-4042`). El puntero avanza con cada fetch
/// (`s->pt = r->pv`, `custom.cpp:12012-12023`) y el canal **ocioso sigue consumiendo
/// words** de la estructura (`generate_sprites`, `custom.cpp:10055-10120`): al terminar
/// los datos de un bloque, la pareja `POS/CTL` del bloque siguiente es la que arma la
/// franja siguiente. La **primera** franja no se deja al walk (una `VSTART` temprana
/// llegaría antes de que la cabecera se consuma): la arma `SpriteManager::arm_dma_chain`
/// por registro, con `SPRxPT` a la DATA del primer bloque (su cabecera del buffer se
/// salta). El terminador `0,0` decodifica `VSTART = VSTOP = 0` y no arma
/// pantalla; tras él el walk solo debe encontrar ceros (el **runway** que deja el
/// llamador al reservar la storage). El `SPRxPT` se recarga cada frame al inicio de la
/// cadena; sin recarga queda tras el terminador (antipatrón *Phantom Sprite*,
/// `docs/reference/emulators/winuae/sprite-dma.md`).
///
/// **Gap obligatorio.** El DMA necesita una línea entre el `VSTOP` de un bloque y el
/// `VSTART` del siguiente para consumir y decodificar su cabecera; el builder **rechaza**
/// bloques que abutan (`kSpriteVerticalGapLines`). Es más estricto que la ocupación del
/// `SpriteAllocator` (que trabaja con `[top,bottom)` y `bottom` exclusivo, de modo que dos
/// placements pueden quedar contiguos): por eso el gap se **exige aquí** y no se hereda.
///
/// **Contrato.** Todo-o-nada por canal: si algo no cuadra (canales mezclados, `attach` en
/// canal par, gap/orden, capacidad, DATA corta) devuelve `false` y no escribe nada — el
/// llamador cae al camino de rearme. La `storage` (Chip, del llamador) se pone a **cero al
/// reservarla**; el builder escribe bloques + terminador y **no toca el resto** (runway).
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/util/array.hpp>
#include <eng/graphics/sprite.hpp>
#include <eng/graphics/sprite_limits.hpp>

namespace eng::graphics {

/// Máximo de bloques por canal con offsets registrados para el parcheo por frame
/// (`patch_sprite_chain_block`). Un canal que reutiliza más franjas que esto no puede
/// usar la estrategia de cadena persistente (el llamador cae a `Rearm`); 32 reusos por
/// canal ya exceden cualquier reparto razonable del frame (el presupuesto real lo decide
/// el selector con la medida).
inline constexpr u8 kSpriteChainMaxBlocks = 32u;

/// **Estructura DMA encadenada de UN canal**, en Chip RAM del llamador.
struct SpriteDmaChain {
	/// Buffer Chip del llamador (a **cero** al reservarlo: el walk tras el terminador solo
	/// debe leer ceros). Tamaño en bytes; la capacidad en words es `size() / 2`.
	eng::ChipView<eng::SpriteTag> storage {};
	u16 used = 0u;  ///< words escritas (bloques + terminador); el runway no cuenta
	/// Offset (words desde el inicio de `storage`) de la `POS` de cada bloque, para
	/// `patch_sprite_chain_block`.
	eng::util::Array<u16, kSpriteChainMaxBlocks> block_pos_words {};
	u8 block_count = 0u;
	u8 channel = 0xffu;  ///< canal de la cadena (0..7) o `0xff` = sin construir
};

/// `POS`/`CTL` codificados de un sprite: la pareja de words que abre su bloque.
struct SpritePosCtl {
	u16 pos = 0u;
	u16 ctl = 0u;
};

/// Codifica `SPRxPOS`/`SPRxCTL` para la cabecera de una estructura DMA (misma convención
/// que `SpriteManager::emit_config`, que pasará a usar esta función al integrar F3;
/// AHRM 3.ª cap. 4, layout verificado contra `../WinUAE-DBG/custom.cpp:4044-4058` y
/// `docs/reference/amiga/techniques/sprite-layer.md` §4):
///
/// - `pos`: bits 15-8 `VSTART[7:0]`, bits 7-0 `HSTART[8:1]` (X en low-res px / 2; los
///   sprites solo ocupan posiciones pares, el bit bajo de X va en el `CTL`);
/// - `ctl`: bits 15-8 `VSTOP[7:0]` (**línea siguiente** a la última visible, exclusiva),
///   bit 7 `ATTACH` (solo canal impar de un par), bit 2 `VSTART[8]`, bit 1 `VSTOP[8]`,
///   bit 0 `HSTART[0]`.
[[nodiscard]] constexpr SpritePosCtl encode_sprite_pos_ctl(u16 hpos, u16 vstart, u16 height,
							   bool attach) noexcept {
	const u16 vstop = static_cast<u16>(vstart + height); // exclusivo (AHRM cap. 4)
	SpritePosCtl e {};
	e.pos = static_cast<u16>(((vstart & 0xffu) << 8u) | ((hpos >> 1u) & 0xffu));
	e.ctl = static_cast<u16>(((vstop & 0xffu) << 8u) | (attach ? 0x0080u : 0x0000u) |
				 (((vstart >> 8u) & 1u) << 2u) | (((vstop >> 8u) & 1u) << 1u) |
				 (hpos & 1u));
	return e;
}

/// Construye la cadena del canal `channel` tomando **solo sus placements** de entre los de
/// un span con varios canales (los demás se ignoran; el span global de `compose_sprites`
/// sale ordenado por `vstart` y esto evita recolectarlos en un buffer temporal).
///
/// Escribe por bloque `POS`, `CTL` y `height*width_words*2` words de DATA (DAT/DATB por
/// línea) y cierra con el terminador `0,0`. Requisitos por canal (todo-o-nada, devuelve
/// `false` sin escribir nada): orden no decreciente de `vstart` con al menos
/// `kSpriteVerticalGapLines` líneas entre el `bottom` de un bloque y el `vstart` del
/// siguiente; `height` recortado a `kSpriteMaxHeightLines` y DATA suficiente
/// (`height*width_words*2` words); `attach` solo en canal impar; a lo sumo
/// `kSpriteChainMaxBlocks` bloques y capacidad en la storage. `used`/`block_count` quedan
/// a 0 al rechazar (el llamador decide la estrategia alternativa, rearme por Copper).
[[nodiscard]] inline bool build_sprite_dma_chain_for_channel(
	eng::Span<const HwSpritePlacement> placements, u8 channel, SpriteDmaChain& out) noexcept {
	out.used = 0u;
	out.block_count = 0u;
	if (channel >= kSpriteChannels || placements.empty() || out.storage.empty()) {
		return false;
	}

	// 1) Validación completa (todo-o-nada) y tamaño total en words.
	eng::usize words = 2u; // terminador
	eng::usize blocks = 0u;
	u16 prev_bottom = 0u;
	for (eng::usize i = 0; i < placements.size(); ++i) {
		const HwSpritePlacement& p = placements[i];
		if (p.channel != channel) {
			continue; // de otro canal: no participa
		}
		const u16 h = p.height > kSpriteMaxHeightLines ? kSpriteMaxHeightLines : p.height;
		if (h == 0u || p.width_words == 0u || p.data.empty()) {
			return false;
		}
		if (p.attach && (channel & 1u) == 0u) {
			return false; // ATTACH (bit 7 del CTL) solo vale en el canal impar
		}
		if (blocks > 0u && p.vstart < static_cast<u16>(prev_bottom + kSpriteVerticalGapLines)) {
			return false; // solape o gap insuficiente: el DMA no reencuentra el canal
		}
		const eng::usize data_words = static_cast<eng::usize>(h) * p.width_words * 2u;
		if (p.data.size() < data_words * 2u) {
			return false; // DATA corta (`ChipView` en bytes)
		}
		words += 2u + data_words;
		prev_bottom = static_cast<u16>(p.vstart + h);
		++blocks;
	}
	if (blocks == 0u || blocks > kSpriteChainMaxBlocks || words > 0xfffeu ||
	    words > out.storage.size() / 2u) {
		return false; // sin bloques, demasiados, o no cabe (u16 para `used`)
	}

	// 2) Escritura (la frontera unsafe hacia Chip queda documentada: la vista certifica
	// el banco, como en `cook_attached_pair`).
	u16* const dst = reinterpret_cast<u16*>(out.storage.address(0).ptr());
	eng::usize cur = 0u;
	for (eng::usize i = 0; i < placements.size(); ++i) {
		const HwSpritePlacement& p = placements[i];
		if (p.channel != channel) {
			continue;
		}
		const u16 h = p.height > kSpriteMaxHeightLines ? kSpriteMaxHeightLines : p.height;
		out.block_pos_words[out.block_count] = static_cast<u16>(cur);
		const SpritePosCtl e = encode_sprite_pos_ctl(p.hpos, p.vstart, h, p.attach);
		dst[cur++] = e.pos;
		dst[cur++] = e.ctl;
		const u16* const src = reinterpret_cast<const u16*>(p.data.address(0).cptr());
		const eng::usize data_words = static_cast<eng::usize>(h) * p.width_words * 2u;
		for (eng::usize k = 0; k < data_words; ++k) {
			dst[cur++] = src[k];
		}
		++out.block_count;
	}
	dst[cur++] = 0u; // terminador: VSTART = VSTOP = 0 → no arma pantalla
	dst[cur++] = 0u;
	out.used = static_cast<u16>(cur);
	out.channel = channel;
	return true;
}

/// Construye la cadena de una lista de placements del **mismo canal** (el caso directo:
/// todos los placements son del canal). Valida la coherencia de canal y delega en
/// `build_sprite_dma_chain_for_channel` (mismas reglas: gap, recortes, capacidad,
/// todo-o-nada).
[[nodiscard]] inline bool build_sprite_dma_chain(eng::Span<const HwSpritePlacement> channel_placements,
						 SpriteDmaChain& out) noexcept {
	out.used = 0u;
	out.block_count = 0u;
	if (channel_placements.empty()) {
		return false;
	}
	const u8 channel = channel_placements[0].channel;
	for (eng::usize i = 0; i < channel_placements.size(); ++i) {
		if (channel_placements[i].channel != channel) {
			return false; // la cadena es de UN canal
		}
	}
	return build_sprite_dma_chain_for_channel(channel_placements, channel, out);
}

/// Parchea `POS`/`CTL` del bloque `block` (4 bytes) **sin tocar su DATA**: es la vía de la
/// **cadena persistente** (F3, `ROADMAP_SPRITES_COMPLETO.md` §3.2.3), para ranuras
/// estables que solo cambian de posición por frame. Aplicarlo tras el último bloque
/// mostrado (VBlank/borde inferior) o con doble cadena; el walk ocioso está en el runway
/// de ceros y no consume los bloques hasta que se recarga `SPRxPT`.
///
/// Devuelve `false` (sin escribir) si el bloque no existe, la altura es 0 o `attach` no
/// cuadra con la paridad del canal de la cadena.
[[nodiscard]] inline bool patch_sprite_chain_block(SpriteDmaChain& chain, u8 block, u16 hpos,
						   u16 vstart, u16 height, bool attach) noexcept {
	if (block >= chain.block_count || chain.storage.empty()) {
		return false;
	}
	const u16 h = height > kSpriteMaxHeightLines ? kSpriteMaxHeightLines : height;
	if (h == 0u || (attach && (chain.channel & 1u) == 0u)) {
		return false;
	}
	u16* const dst = reinterpret_cast<u16*>(chain.storage.address(0).ptr());
	u16* const w = dst + chain.block_pos_words[block];
	const SpritePosCtl e = encode_sprite_pos_ctl(hpos, vstart, h, attach);
	w[0] = e.pos;
	w[1] = e.ctl;
	return true;
}

/// **Estrategia de emisión de un canal** (F3.3):
///
/// - `Rearm`: camino actual (`emit_placements_into` sin cadena: primera config + rearme por
///   franja con Copper). Por defecto y para todo lo que no cumpla lo demás.
/// - `Chain`: cadena **reconstruida** este frame por el llamador (reutilizable cuando las
///   imágenes/alturas cambian o las ranuras no son estables).
/// - `ChainPersistent`: cadena construida en un frame anterior y **parcheada** solo en sus
///   `POS`/`CTL` (`patch_sprite_chain_block`); exige ranuras estables (orden por Y y alturas
///   constantes) — el caso de las ristras de tiros.
enum class SpriteEmitStrategy : u8 {
	Rearm = 0u,
	Chain = 1u,
	ChainPersistent = 2u,
};

/// Plan de emisión por canal (índice = canal 0..7).
struct SpriteEmitPlan {
	SpriteEmitStrategy strategy[kSpriteChannels] {};
};

/// Presupuesto del selector: **words de DATA** (Σ de los bloques + cabeceras) que se aceptan
/// para reconstruir una cadena por frame. El umbral se fija con la **medida A/B** (rearme
/// vs copia; `BUS_BUDGET.md`): con `max_data_words = 0` no se eligen cadenas nuevas (solo
/// `ChainPersistent`, que no recopia DATA).
struct SpriteChainBudget {
	u32 max_data_words = 0u;
};

/// **Decide la estrategia por canal** (una vez por frame, lógica pura):
///
/// - cuenta los placements válidos y su DATA por canal (mismo criterio que el builder);
/// - un canal con cadena **lista** (`chains[c]` construida, del canal `c` y con
///   `block_count` == placements válidos de `c`) y **≥2 bloques** pasa a `Chain` — o a
///   `ChainPersistent` si su bit está en `stable_channels_mask` (ranuras estables);
/// - `Chain` exige además que la DATA del canal quepa en `budget.max_data_words`;
/// - el resto queda en `Rearm`.
///
/// El llamador construye las cadenas **antes** de llamar (solo los canales que interesen) y,
/// con el plan, emite con `emit_placements_into(..., chains)`; los canales `Chain` deben
/// rehacerse cada frame, los `ChainPersistent` solo parchearse.
namespace sprite_chain_detail {

/// Placements válidos y DATA (words) por canal (mismo criterio que el builder).
struct ChannelStats {
	u8 blocks[kSpriteChannels] {};
	u32 data_words[kSpriteChannels] {};
};

[[nodiscard]] inline ChannelStats channel_stats(
	eng::Span<const HwSpritePlacement> placements) noexcept {
	ChannelStats st {};
	for (eng::usize i = 0; i < placements.size(); ++i) {
		const HwSpritePlacement& p = placements[i];
		if (p.channel >= kSpriteChannels || p.data.empty() || p.height == 0u ||
		    p.width_words == 0u) {
			continue;
		}
		const u16 h = p.height > kSpriteMaxHeightLines ? kSpriteMaxHeightLines : p.height;
		++st.blocks[p.channel];
		st.data_words[p.channel] += 2u + static_cast<u32>(h) * p.width_words * 2u;
	}
	return st;
}

} // namespace sprite_chain_detail

[[nodiscard]] inline SpriteEmitPlan
choose_sprite_emit_plan(eng::Span<const HwSpritePlacement> placements,
			eng::Span<const SpriteDmaChain> chains, u8 stable_channels_mask,
			const SpriteChainBudget& budget = {}) noexcept {
	SpriteEmitPlan plan {};
	const sprite_chain_detail::ChannelStats st = sprite_chain_detail::channel_stats(placements);
	for (u8 c = 0u; c < kSpriteChannels; ++c) {
		bool ready = st.blocks[c] >= 2u && chains.size() > c;
		if (ready) {
			const SpriteDmaChain& ch = chains.data()[c];
			ready = ch.used != 0u && ch.channel == c && ch.block_count == st.blocks[c];
		}
		if (!ready) {
			plan.strategy[c] = SpriteEmitStrategy::Rearm;
			continue;
		}
		if ((stable_channels_mask & (1u << c)) != 0u) {
			plan.strategy[c] = SpriteEmitStrategy::ChainPersistent;
		} else if (budget.max_data_words != 0u && st.data_words[c] <= budget.max_data_words) {
			plan.strategy[c] = SpriteEmitStrategy::Chain;
		} else {
			plan.strategy[c] = SpriteEmitStrategy::Rearm;
		}
	}
	return plan;
}

/// **Acción de cadena que el compositor debe ejecutar por canal** (el paso que falta entre
/// el selector y la emisión, para la fachada de juego):
///
/// - `Rearm`: no tocar cadenas (el emisor usará el camino de rearmes);
/// - `Build`: **reconstruir** la cadena del canal desde los placements de este frame
///   (`build_sprite_dma_chain_for_channel`); exige ≥2 bloques, storage presente y que la
///   DATA del canal quepa en `budget.max_data_words`;
/// - `Keep`: la cadena existente **cubre** el canal (mismos bloques y ranuras estables:
///   su bit en `stable_channels_mask`) y solo se **parchea** (`patch_sprite_chain_block`).
enum class SpriteChainAction : u8 {
	Rearm = 0u,
	Build = 1u,
	Keep = 2u,
};

/// Plan de acciones por canal (índice = canal 0..7).
struct SpriteChainPlan {
	SpriteChainAction action[kSpriteChannels] {};
};

/// Decide la acción por canal (misma estadística y criterios que `choose_sprite_emit_plan`).
[[nodiscard]] inline SpriteChainPlan choose_sprite_chain_actions(
	eng::Span<const HwSpritePlacement> placements, eng::Span<const SpriteDmaChain> chains,
	u8 stable_channels_mask, const SpriteChainBudget& budget = {}) noexcept {
	SpriteChainPlan plan {};
	const sprite_chain_detail::ChannelStats st = sprite_chain_detail::channel_stats(placements);
	for (u8 c = 0u; c < kSpriteChannels; ++c) {
		const bool have_storage = chains.size() > c && !chains.data()[c].storage.empty();
		if (st.blocks[c] < 2u || !have_storage) {
			plan.action[c] = SpriteChainAction::Rearm;
			continue;
		}
		const SpriteDmaChain& ch = chains.data()[c];
		const bool covers = ch.used != 0u && ch.channel == c && ch.block_count == st.blocks[c];
		if ((stable_channels_mask & (1u << c)) != 0u && covers) {
			plan.action[c] = SpriteChainAction::Keep;
		} else if (budget.max_data_words != 0u && st.data_words[c] <= budget.max_data_words) {
			plan.action[c] = SpriteChainAction::Build;
		} else {
			plan.action[c] = SpriteChainAction::Rearm;
		}
	}
	return plan;
}

/// Resultado de preparar las cadenas del frame.
struct SpriteChainPrep {
	SpriteEmitPlan emit {};  ///< plan efectivo para `emit_placements_into(..., chains)`
	u8 built = 0u;           ///< canales reconstruidos este frame
	u8 kept = 0u;            ///< canales persistentes conservados
};

/// **Prepara las cadenas del frame** (F3, integración con el compositor de juego): decide
/// la acción por canal y la ejecuta —
///
///   - `Build`: `build_sprite_dma_chain_for_channel`; si no cabe (todo-o-nada), la storage
///     queda a cero y la estrategia efectiva cae a `Rearm` (el emisor rearma ese canal);
///   - `Keep`: no se toca la cadena (el llamador la parchea con `patch_sprite_chain_block`
///     cuando mueve las ranuras; las imágenes y alturas deben ser estables);
///   - `Rearm`: nada.
///
/// Es el puente que convierte la salida de `compose_sprites`/`SpriteScene` en cadenas
/// listas para `emit_placements_into(..., chains)` sin que el juego conozca el detalle.
[[nodiscard]] inline SpriteChainPrep prepare_sprite_chains(
	eng::Span<const HwSpritePlacement> placements, eng::Span<SpriteDmaChain> chains,
	u8 stable_channels_mask, const SpriteChainBudget& budget = {}) noexcept {
	SpriteChainPrep prep {};
	const SpriteChainPlan plan =
		choose_sprite_chain_actions(placements, chains, stable_channels_mask, budget);
	const u8 limit = chains.size() < kSpriteChannels ? static_cast<u8>(chains.size())
							 : kSpriteChannels;
	for (u8 c = 0u; c < limit; ++c) {
		switch (plan.action[c]) {
		case SpriteChainAction::Build:
			if (build_sprite_dma_chain_for_channel(placements, c, chains.data()[c])) {
				prep.emit.strategy[c] = SpriteEmitStrategy::Chain;
				++prep.built;
			} else {
				prep.emit.strategy[c] = SpriteEmitStrategy::Rearm; // no cabe: rearme
			}
			break;
		case SpriteChainAction::Keep:
			prep.emit.strategy[c] = SpriteEmitStrategy::ChainPersistent;
			++prep.kept;
			break;
		default:
			prep.emit.strategy[c] = SpriteEmitStrategy::Rearm;
			break;
		}
	}
	return prep;
}

} // namespace eng::graphics
