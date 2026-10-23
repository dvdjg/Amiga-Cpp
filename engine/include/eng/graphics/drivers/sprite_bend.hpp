#pragma once

/// \file sprite_bend.hpp
/// **Bending de un sprite por línea** (F4.2 de `ROADMAP_SPRITES_COMPLETO.md` §3.3): el
/// Copper reescribe `SPRxPOS`/`SPRxCTL` al principio de **cada línea** del sprite, con la X
/// tomada de una tabla precalculada (seno u otra onda). Al escribir `SPRxCTL` con
/// `VSTART = línea actual`, el registro **rearma el DMA** de ese canal cada línea y la fila
/// siguiente se dibuja con la X nueva (`../WinUAE-DBG/custom.cpp:4078-4090`: la cabecera
/// procesada con `dmastate == 0` llama `sprstartstop`); el walk aporta una fila de DATA por
/// línea (`custom.cpp:12012-12023`). Es la misma carrera contra el haz que el efecto Jim
/// Power (demo 215): **medir** antes de dar por buena una altura grande.
///
/// **Contrato de DATA.** `data` debe ser el bloque de filas del sprite **más un terminador
/// `0,0`** (`height*2 + 2` words): tras la última fila el walk queda en el terminador y no
/// lee como cabecera lo que siga (antipatrón *Phantom Sprite*,
/// `docs/reference/emulators/winuae/sprite-dma.md`).
///
/// **Tabla de offsets.** Potencia de dos (p. ej. 128/256; el índice se enmascara); `phase`
/// (0..255) y `step` (1 = un offset por línea; 2 = uno cada 2 líneas, mitad de coste) dan
/// la animación. La X final se recorta a `>= 0` (un sprite no puede salir por la
/// izquierda).
///
/// `height` se recorta a `kSpriteMaxHeightLines`. No emite nada (rechazo controlado) si el
/// canal es inválido, la tabla no es potencia de dos o la DATA no cubre filas+terminador.
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable con un scheduler real.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/sprite_chain.hpp>
#include <eng/graphics/sprite_limits.hpp>

namespace eng::graphics::drivers {

/// Un canal doblado: canal, DATA (filas + terminador), X base y fase.
struct SpriteBend {
	u8 channel = 0u;
	eng::ChipView<eng::SpriteTag> data {}; ///< filas del sprite + terminador `0,0`
	u16 x_base = 0u;
	u8 phase = 0u; ///< fase inicial (0..255) para animar
};

/// Emite el bending de **varios canales a la vez**, intercalado por línea (un solo `WAIT`
/// compartido por línea y luego los `POS`/`CTL` de cada canal): por cada línea de
/// `[base_y, base_y + height)` y cada sprite `s`, `x = s.x_base + offsets[(s.phase + j) & mask]`
/// (`j` avanza 1 por cada `step` líneas) con `VSTART`/`VSTOP` de la línea y el fin del
/// sprite. `SPRxPT` **no** se toca: lo dejó apuntando a la DATA el armado del objeto.
///
/// Emitir cada canal por separado en bloques **no** vale: los `WAIT` del segundo canal
/// quedarían después de los del primero (fuera de orden por línea) — de ahí la emisión
/// intercalada. Rechazo controlado (sin emitir) si la tabla no es potencia de dos, la
/// altura es 0, o **ningún** sprite tiene canal válido y DATA suficiente (`height*2 + 2`
/// words); los sprites inválidos se saltan.
///
/// \param sched    scheduler de Copper (se emite en orden por línea).
/// \param sprites  canales doblados (canal + DATA + X base + fase).
/// \param base_y   primera línea de los sprites.
/// \param height   líneas del sprite (recortado a `kSpriteMaxHeightLines`).
/// \param offsets  tabla de offsets X (potencia de dos; típ. 128/256).
/// \param step     líneas por offset (1 = por línea; 2 = cada 2; 0 se corrige a 1).
/// \tparam Sched tipo de emisor de Copper/scheduler
template <class Sched>
inline void emit_sprite_bend_into(Sched& sched, eng::Span<const SpriteBend> sprites,
				  u16 base_y, u16 height, eng::Span<const s16> offsets,
				  u8 step = 1u) {
	if (sprites.empty() || height == 0u || offsets.size() < 2u) {
		return;
	}
	const eng::usize mask = offsets.size() - 1u;
	if ((offsets.size() & mask) != 0u) {
		return; // la tabla debe ser potencia de dos (índice por máscara, sin `%`)
	}
	const u16 h = height > kSpriteMaxHeightLines ? kSpriteMaxHeightLines : height;
	if (step == 0u) {
		step = 1u;
	}
	const u16 end = static_cast<u16>(base_y + h); // VSTOP exclusivo
	const eng::usize need_bytes = (static_cast<eng::usize>(h) * 2u + 2u) * 2u;
	eng::u16 j = 0u;
	u8 in_step = 0u;
	for (u16 i = 0u; i < h; ++i) {
		const u16 line = static_cast<u16>(base_y + i);
		bool waited = false;
		for (eng::usize k = 0u; k < sprites.size(); ++k) {
			const SpriteBend& s = sprites.data()[k];
			if (s.channel >= kSpriteChannels || s.data.size() < need_bytes) {
				continue; // sprite inválido: se salta (no invalida a los demás)
			}
			if (!waited) {
				sched.wait_line_safe(line);
				waited = true;
			}
			const eng::usize idx = (static_cast<eng::usize>(s.phase) + j) & mask;
			eng::s32 x = static_cast<eng::s32>(s.x_base) + offsets.data()[idx];
			if (x < 0) {
				x = 0; // un sprite no puede salir por la izquierda
			}
			const SpritePosCtl e = encode_sprite_pos_ctl(static_cast<u16>(x), line,
								     static_cast<u16>(end - line), false);
			sched.move(copper::sprite_pos_register(s.channel), e.pos); // SPRxPOS
			sched.move(copper::sprite_ctl_register(s.channel), e.ctl); // SPRxCTL
		}
		if (++in_step >= step) {
			in_step = 0u;
			++j;
		}
	}
}

/// Azúcar de un solo canal: misma emisión con un `SpriteBend` efímero.
/// \tparam Sched tipo de emisor de Copper/scheduler
template <class Sched>
inline void emit_sprite_bend_into(Sched& sched, u8 channel, eng::ChipView<eng::SpriteTag> data,
				  u16 base_y, u16 height, u16 x_base,
				  eng::Span<const s16> offsets, u8 phase, u8 step = 1u) {
	const SpriteBend one {channel, data, x_base, phase};
	emit_sprite_bend_into(sched, eng::Span<const SpriteBend> {&one, 1u}, base_y, height,
			      offsets, step);
}

} // namespace eng::graphics::drivers
