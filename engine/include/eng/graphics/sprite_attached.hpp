#pragma once

/// \file sprite_attached.hpp
/// **Cocinado de la DATA de un par de sprites *attached*** (15 colores + transparente).
///
/// Dos canales del **mismo par** (0+1, 2+3, 4+5, 6+7), dibujados en la **misma posición**,
/// se unen poniendo el bit `ATTACH` (bit 7 de `SPRxCTL`) en el canal **impar**: el par pasa
/// de dos objetos de 3 colores a **un sprite de 16 px con 15 colores**. El par aporta bits
/// 0-1 (canal par) y bits 2-3 (canal impar) de un índice de 4 bits sobre `COLOR16-31`
/// (AHRM 3.ª cap. 4, «Attached Sprites», Table 4-5: índice 0 = transparente, `COLOR16`
/// sin uso; 1..15 = `COLOR17..31`). Referencia local:
/// `docs/reference/amiga/techniques/sprite-layer.md` §4.
///
/// La unidad que consume el DMA de sprites es la **estructura con cabecera**
/// (`winuae/sprite-dma.md`): `POS`, `CTL`, `DAT`/`DATB` por línea y terminador `0,0`.
/// `SPRxPT` apunta al **inicio** (la cabecera); un buffer con solo `DAT/DATB` se
/// interpreta como cabecera y deja el canal mal armado. Este módulo convierte un bitmap
/// planar de **4 planos** en las dos estructuras del par (una por canal) con el bit
/// `ATTACH` ya puesto en la impar; es el ladrillo que
/// `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md` §6 dejaba pendiente («falta un
/// helper que cocine la DATA de 4 planos»).
///
/// **Memoria DMA tipada por banco.** Las estructuras son leídas por Agnus (`SPRxPT`), así
/// que su `Span` no basta: la API las recibe como `ChipView<SpriteTag>`
/// (`MemView<Tag, MemoryKind::Chip>`) y la escritura interna baja a `Address<Chip>::ptr()`
/// (la frontera unsafe documentada; mismo patrón que `graphics/c2p.hpp`). Pasar memoria
/// Fast/Slow o una vista sin banco **no compila** (regla de `AGENTS.md` §1.10 y
/// `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md` §3.6).
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.
///
/// ```cpp
/// // 4 planos contiguos de 16 px: src[p*height + row]; pos/ctl ya codificados.
/// constexpr eng::usize bytes = eng::graphics::attached_pair_structure_words(h) * 2u;
/// eng::ChipView<eng::SpriteTag> even = block.mem_view().subview(0u, bytes);
/// eng::ChipView<eng::SpriteTag> odd = block.mem_view().subview(bytes, bytes);
/// eng::graphics::cook_attached_pair(src4, h, pos, ctl, even, odd);
/// // Canal par:   SPRxPT -> even (attach NO)
/// // Canal impar: SPRxPT -> odd  (attach SI; ATTACH queda en su CTL)
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>

namespace eng::graphics {

/// Words de la **estructura DMA** de un canal del par (`attached`): cabecera `POS`+`CTL`
/// (2) + `DAT`/`DATB` por línea (`height*2`) + terminador a cero (2). Es el tamaño mínimo
/// (en words; ×2 para bytes) que espera `cook_attached_pair` por canal. Ver el formato en
/// `docs/reference/emulators/winuae/sprite-dma.md`.
[[nodiscard]] constexpr u16 attached_pair_structure_words(u16 height) noexcept {
	return (height << 1u) + 4u;
}

/// **Cocina las dos estructuras DMA de un par *attached*.** Divide un bitmap planar de
/// **4 planos** y `height` líneas (16 px de ancho, 1 word por plano y fila) en los dos
/// canales del par y escribe en cada uno su estructura completa `[POS, CTL, DATA…, 0,0]`
/// en **Chip RAM** (`ChipView<SpriteTag>`).
///
/// Formato de `src` (planos **contiguos** y agnósticos de banco —es arte de CPU—,
/// `src.size() >= 4*height`): el plano `p` ocupa `[p*height, (p+1)*height)`, y el índice
/// de color del píxel es `p0 | p1<<1 | p2<<2 | p3<<3` (0 = transparente).
///
/// `pos` es el `SPRxPOS` codificado del par (misma posición para ambos canales) y `ctl` el
/// `SPRxCTL` **sin** el bit `ATTACH`. La estructura **par** recibe los planos **0-1** (bits
/// bajos del índice) con `ctl` tal cual; la **impar** recibe los planos **2-3** (bits altos)
/// con el bit 7 (`ATTACH`) forzado. Separar los dos canales (o poner `ATTACH` en el par)
/// cambia el color de los píxeles: ambos deben programarse en la misma posición.
///
/// El llamador actualiza `SPRxPOS`/`SPRxCTL`/`SPRxPT` por frame (el `SPRxPT` debe
/// **recargarse** cada frame: sin recarga queda tras el terminador — antipatrón
/// *Phantom Sprite*, `sprite-layer.md` §10). Si el Copper reescribe POS/CTL al armar el
/// canal en su línea, conviene cocinar la cabecera **OFF** (`POS=CTL=0xfe00`,
/// `VSTART=VSTOP=254`): un canal desarmado que lea la cabecera no se auto-arma con ella.
/// `pos`/`ctl` son del llamador para permitir también el modo DMA auto-armado (la cabecera
/// es lo que Agnus lee al final del VBlank).
///
/// \return `false` (sin escribir nada) si `height == 0`, hay punteros nulos o alguna vista
///         no cubre lo necesario (bytes de cada estructura).
[[nodiscard]] inline bool cook_attached_pair(Span<const u16> src, u16 height, u16 pos, u16 ctl,
					     ChipView<SpriteTag> even, ChipView<SpriteTag> odd) noexcept {
	const usize h = height;                            // u16 -> usize (sin cast)
	const usize need_src = h * 4u;
	const usize need_out = attached_pair_structure_words(height); // words
	const usize need_out_bytes = need_out * 2u;                   // bytes
	if (height == 0u || src.data() == nullptr || even.data() == nullptr || odd.data() == nullptr ||
	    src.size() < need_src || even.size() < need_out_bytes || odd.size() < need_out_bytes) {
		return false;
	}
	// Frontera unsafe documentada: la vista Chip certifica el banco; la cocina escribe words.
	u16* const ev = reinterpret_cast<u16*>(even.address(0).ptr());
	u16* const od = reinterpret_cast<u16*>(odd.address(0).ptr());
	ev[0] = pos;
	ev[1] = ctl;
	od[0] = pos;
	od[1] = ctl | 0x0080u; // bit 7: ATTACH, solo valido en el impar
	// Plano contiguo `p` = src[p*height + row]; los offsets de plano se precalculan.
	const usize p2 = h << 1u;
	const usize p3 = p2 + h;
	for (u16 row = 0; row < height; ++row) {
		// `i` = DAT,DATB dentro de la estructura; bit 15 = píxel 0 (izquierda).
		const usize i = row * 2u;
		ev[2u + i] = src[row];           // DAT: plano 0 -> bit 0 del índice
		ev[2u + i + 1u] = src[h + row];  // DATB: plano 1 -> bit 1
		od[2u + i] = src[p2 + row];      // DAT: plano 2 -> bit 2
		od[2u + i + 1u] = src[p3 + row]; // DATB: plano 3 -> bit 3
	}
	const usize term = need_out - 2u;
	ev[term] = 0u;
	ev[term + 1u] = 0u;
	od[term] = 0u;
	od[term + 1u] = 0u;
	return true;
}

} // namespace eng::graphics
