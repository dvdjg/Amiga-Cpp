#pragma once

/// \file sprite_palette.hpp
/// **Necesidades de paleta de los Sprites HW y su arbitraje** (F7 de
/// `ROADMAP_JUEGO_SPRITES_BOBS.md` §5).
///
/// Hechos de hardware (AHRM 3.ª cap. 4; `docs/reference/amiga/techniques/sprite-layer.md` §3-§4):
///
/// - **Sin attached**, cada **par** de canales usa cuatro registros: par `p` → `COLOR16+4·p`
///   .. `+3` (0/1 → 16-19, 2/3 → 20-23, 4/5 → 24-27, 6/7 → 28-31); el índice de 2 bits
///   selecciona 16/17/18/19 (0 = transparente).
/// - **Con attached**, el índice de 4 bits del par selecciona `COLOR17..31` (Table 4-5): un
///   par *attached* lee **todo** el fichero 16-31, así que dos pares *attached* con paletas
///   distintas no pueden convivir en las mismas líneas.
/// - **Verticalmente**, un mismo canal reusado en franjas disjuntas puede **conmutar** su
///   paleta: el Copper escribe los `COLORxx` en la línea de entrada (`PaletteLine`).
///
/// Este módulo es el **planificador puro**: recibe las necesidades por sprite (canal ya
/// asignado, registros que usa, franja visible, paleta y `z`) y decide por uso:
///
/// - `Ok`: sin conflicto;
/// - `CopperSwitch`: hay que escribir sus colores en su `top` (emite el `CopperIntent`);
/// - `Degrade`: conflicto irresoluble (visible a la vez que otro sprite con paleta distinta
///   en registros compartidos) → el de **menor `z`** (y, en empate, el de índice mayor) debe
///   materializarse como BOB.
///
/// La decisión de canal/par la toma el `SpriteAllocator`; este planificador solo arbitra la
/// paleta una vez conocidos los canales. Lógica pura (sin hardware, sin heap): host-testable.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics {

/// Intervalo de registros `COLORxx` que usa un sprite y la franja en la que es visible.
struct SpritePaletteNeed {
	u8 first = 16;   ///< primer registro (0-based, p. ej. 16 o 17)
	u8 count = 0;    ///< registros usados (0 = no declara paleta)
	u16 top = 0;     ///< primera línea visible (inclusive)
	u16 bottom = 0;  ///< línea final (exclusiva)
};

/// Necesidad de paleta **de un sprite ya colocado**: canal, registros/franja, paleta
/// (puntero de identidad) y prioridad `z` (mayor = delante).
struct SpritePaletteUse {
	u8 channel = 0;
	SpritePaletteNeed need {};
	const u16* colors = nullptr;  ///< palabras COLOR de su paleta (identidad por puntero)
	u8 z = 0;
};

/// Resultado por uso.
enum class SpritePaletteDecision : u8 {
	Ok = 0,         ///< su paleta ya está vigente en su franja
	CopperSwitch,   ///< hay que escribir su paleta en `need.top` (intención emitida)
	Degrade,        ///< conflicto irresoluble: materializar como BOB
};

namespace sprite_palette_detail {

[[nodiscard]] constexpr bool lines_overlap(const SpritePaletteNeed& a,
					   const SpritePaletteNeed& b) noexcept {
	return a.top < b.bottom && b.top < a.bottom;
}

[[nodiscard]] constexpr bool regs_overlap(const SpritePaletteNeed& a,
					  const SpritePaletteNeed& b) noexcept {
	return a.count != 0u && b.count != 0u && a.first < static_cast<u8>(b.first + b.count) &&
	       b.first < static_cast<u8>(a.first + a.count);
}

/// ¿El uso `i` pierde el conflicto frente a `j`? (menor `z`; empate: índice mayor).
[[nodiscard]] constexpr bool lower_priority(usize i, u8 zi, usize j, u8 zj) noexcept {
	return zi < zj || (zi == zj && i > j);
}

} // namespace sprite_palette_detail

/// **Arbitra las necesidades de paleta** de un frame.
///
/// `out` recibe una decisión por uso (mismo tamaño que `uses`; usos sin sitio se marcan
/// `Degrade`). `copper_out` recibe las intenciones `PaletteLine` de las conmutaciones
/// (una por conmutación que quepa; el resto no se emite). Devuelve cuántas intenciones
/// escribió.
inline u16 plan_sprite_palettes(Span<const SpritePaletteUse> uses,
				Span<SpritePaletteDecision> out,
				Span<CopperIntent> copper_out) noexcept {
	using namespace sprite_palette_detail;
	const usize n = uses.size();
	u16 copper = 0u;
	for (usize i = 0u; i < n; ++i) {
		const SpritePaletteUse& ui = uses[i];
		if (i < out.size()) {
			out[i] = SpritePaletteDecision::Ok;
		}
		if (ui.need.count == 0u || ui.colors == nullptr) {
			continue;
		}
		// 1) Conflicto simultáneo (líneas y registros solapados con paleta distinta).
		bool degrade = false;
		for (usize j = 0u; j < n; ++j) {
			if (j == i) {
				continue;
			}
			const SpritePaletteUse& uj = uses[j];
			if (uj.need.count == 0u || uj.colors == nullptr ||
			    uj.colors == ui.colors) {
				continue;
			}
			if (lines_overlap(ui.need, uj.need) && regs_overlap(ui.need, uj.need) &&
			    lower_priority(i, ui.z, j, uj.z)) {
				degrade = true;
				break;
			}
		}
		if (degrade) {
			if (i < out.size()) {
				out[i] = SpritePaletteDecision::Degrade;
			}
			continue;
		}
		// 2) ¿Hace falta conmutar? Alguien escribió estos registros antes (franja disjunta)
		// con otra paleta.
		bool need_switch = false;
		for (usize j = 0u; j < n; ++j) {
			if (j == i) {
				continue;
			}
			const SpritePaletteUse& uj = uses[j];
			if (uj.need.count == 0u || uj.colors == nullptr ||
			    uj.colors == ui.colors) {
				continue;
			}
			if (uj.need.bottom <= ui.need.top && regs_overlap(ui.need, uj.need)) {
				need_switch = true;
				break;
			}
		}
		if (!need_switch) {
			continue;
		}
		if (i < out.size()) {
			out[i] = SpritePaletteDecision::CopperSwitch;
		}
		if (copper < copper_out.size()) {
			CopperIntent& c = copper_out[copper++];
			c = CopperIntent {};
			c.kind = CopperIntentKind::PaletteLine;
			c.top = ui.need.top;
			c.bottom = ui.need.top;
			c.colors = PaletteWords {ui.colors, ui.need.count};
			c.first = ui.need.first;
			c.count = ui.need.count;
		}
	}
	return copper;
}

} // namespace eng::graphics
