#pragma once

/// \file scroll_plan.hpp
/// **Planificador de scroll adaptativo** (`eng::scene`): elige el algoritmo efectivo de una
/// región/capa degradando si el presupuesto de Copper no da, y **estima la memoria** de cada
/// técnica al reservar. Es la pieza que permite «la capa pide (`ScrollKind`), el planner
/// dispone» sin que la app decida registros ni límites.
///
/// Escalera de degradación (Copper por línea): `CopperSplit → CopperRing → Fine → None`.
/// `BlitterColumns` es ortogonal (no usa Copper); se conserva tal cual.
///
/// Ver `ROADMAP_API_COHERENCE.md` §7.3 y `OBJECT_SYSTEM.md` §15.

#include <eng/core/types/types.hpp>
#include <eng/scene/world.hpp>

namespace eng::scene {

/// Palabras de Copper por línea que consume una técnica de scroll.
[[nodiscard]] constexpr u16 scroll_copper_per_line(ScrollKind k) noexcept {
	switch (k) {
	case ScrollKind::CopperSplit:
		return 4u; ///< split por línea (xyunlimited)
	case ScrollKind::CopperRing:
		return 2u; ///< reapuntado/módulo (xlimited)
	default:
		return 0u; ///< Fine/BlitterColumns/None: sin Copper por línea
	}
}

/// Técnica siguiente en la escalera de degradación.
[[nodiscard]] constexpr ScrollKind degrade_scroll(ScrollKind k) noexcept {
	switch (k) {
	case ScrollKind::CopperSplit:
		return ScrollKind::CopperRing;
	case ScrollKind::CopperRing:
		return ScrollKind::Fine;
	case ScrollKind::Fine:
		return ScrollKind::None;
	default:
		return ScrollKind::None;
	}
}

/// **Scroll efectivo**: degrada `requested` hasta que su coste de Copper por línea quepa en
/// `copper_available_per_line`. `BlitterColumns` no se degrada (no usa Copper).
[[nodiscard]] constexpr ScrollKind choose_scroll(ScrollKind requested,
						 u16 copper_available_per_line) noexcept {
	if (requested == ScrollKind::BlitterColumns || requested == ScrollKind::None) {
		return requested;
	}
	ScrollKind k = requested;
	while (k != ScrollKind::None && scroll_copper_per_line(k) > copper_available_per_line) {
		k = degrade_scroll(k);
	}
	return k;
}

/// Estimación de memoria de una capa de scroll.
struct ScrollMemory {
	u32 bytes = 0u;   ///< ventana completa (todos los planos), en bytes
	u16 window_w = 0u; ///< ancho de la ventana en píxeles (visible + guardas)
	u16 window_h = 0u; ///< alto de la ventana en píxeles
};

/// Bytes por fila de un plano sin intercalar (palabras de 16 px).
[[nodiscard]] constexpr u16 planar_row_bytes(u16 w) noexcept {
	return static_cast<u16>(((static_cast<u32>(w) + 15u) / 16u) * 2u);
}

/// **Memoria de la ventana** de una técnica. `speed_px` = velocidad máxima de scroll (px/frame):
/// acota las **bandas de guarda** de `CopperRing`/`BlitterColumns` (a más velocidad, más guarda
/// para esconder la actualización). `CopperSplit` (xyunlimited) usa márgenes fijos (ring).
[[nodiscard]] constexpr ScrollMemory scroll_memory(ScrollKind kind, u16 visible_w, u16 visible_h,
						   u8 planes, u8 speed_px) noexcept {
	ScrollMemory m {};
	if (kind == ScrollKind::CopperSplit) {
		m.window_w = static_cast<u16>(visible_w + 16u);
		m.window_h = static_cast<u16>(visible_h + 16u);
	} else if (kind == ScrollKind::CopperRing || kind == ScrollKind::BlitterColumns) {
		const u16 guard = static_cast<u16>(speed_px + 8u);
		m.window_w = static_cast<u16>(visible_w + 2u * guard);
		m.window_h = static_cast<u16>(visible_h + 2u * guard);
	} else {
		m.window_w = visible_w;
		m.window_h = visible_h;
	}
	m.bytes = static_cast<u32>(planar_row_bytes(m.window_w)) * m.window_h * planes;
	return m;
}

/// **Geometría de un anillo de scroll** (ring con márgenes de guarda en ambos ejes): ventana =
/// visible + `2·margen`, bytes por plano y total. Es la base de los drivers de scroll con
/// guardas (`CopperRing`/`CopperSplit`/`BlitterColumns`) para reservar y direccionar.
struct ScrollRing {
	u16 window_w = 0u; ///< ancho de la ventana (visible + 2·margen_x)
	u16 window_h = 0u; ///< alto de la ventana (visible + 2·margen_y)
	u16 margin_x = 0u;
	u16 margin_y = 0u;
	u16 row_bytes = 0u; ///< bytes por fila de un plano
	u32 bytes = 0u;     ///< total (todos los planos)
};

/// Construye la geometría del anillo para `planes` y márgenes dados.
[[nodiscard]] constexpr ScrollRing scroll_ring(u16 visible_w, u16 visible_h, u8 planes,
					       u16 margin_x, u16 margin_y) noexcept {
	ScrollRing r {};
	r.margin_x = margin_x;
	r.margin_y = margin_y;
	r.window_w = static_cast<u16>(visible_w + 2u * margin_x);
	r.window_h = static_cast<u16>(visible_h + 2u * margin_y);
	r.row_bytes = planar_row_bytes(r.window_w);
	r.bytes = static_cast<u32>(r.row_bytes) * r.window_h * planes;
	return r;
}

/// Columnas (o filas) de tile **cruzadas** al pasar el scroll de `from` a `to` (px): cuántas
/// bandas nuevas hay que rellenar en el anillo (0 si no se cruza ningún tile). Devuelve el valor
/// absoluto (sirve para cualquier sentido).
[[nodiscard]] constexpr u16 ring_crossed(u16 from, u16 to, u16 tile_size) noexcept {
	if (tile_size == 0u) {
		return 0u;
	}
	const u16 a = static_cast<u16>(from / tile_size);
	const u16 b = static_cast<u16>(to / tile_size);
	return (b >= a) ? static_cast<u16>(b - a) : static_cast<u16>(a - b);
}

/// Elige la mejor técnica cuyo **coste de Copper** quepa y cuya **memoria** quepa en
/// `chip_available`. Degrada desde `requested`.
[[nodiscard]] constexpr ScrollKind choose_scroll_fitting(ScrollKind requested, u16 visible_w,
							 u16 visible_h, u8 planes, u8 speed_px,
							 u16 copper_available_per_line,
							 u32 chip_available) noexcept {
	ScrollKind k = choose_scroll(requested, copper_available_per_line);
	while (k != ScrollKind::None) {
		const ScrollMemory m = scroll_memory(k, visible_w, visible_h, planes, speed_px);
		if (m.bytes <= chip_available) {
			return k;
		}
		k = degrade_scroll(k);
	}
	return ScrollKind::None;
}

/// Presupuesto del planner para una región.
struct RegionBudget {
	u16 copper_per_line = 0xffffu;    ///< MOVEs de Copper disponibles por línea
	u32 chip_available = 0xffffffffu; ///< bytes de Chip disponibles
};

/// Resultado de planificar una región: la técnica de scroll **efectiva**, su coste y su memoria.
struct RegionPlan {
	ScrollKind scroll = ScrollKind::None;
	RegionCost cost {};
	ScrollMemory memory {};
	bool ok = false; ///< `false` si ni la técnica mínima cabe en el presupuesto
};

/// **Planifica una región**: elige el `ScrollKind` efectivo (degradando) que quepa en Copper y
/// Chip, con su `RegionCost` y su `ScrollMemory`. La capa **pide** (`region.scroll`,
/// `region.speed_px`); el planner **dispone**. La materialización (playfield/Copper) va encima.
[[nodiscard]] constexpr RegionPlan plan_region(const WorldRegion& region, u16 visible_w,
					       u16 visible_h, RegionBudget budget) noexcept {
	RegionPlan p {};
	p.scroll = choose_scroll_fitting(region.scroll, visible_w, visible_h, region.planes,
					 region.speed_px, budget.copper_per_line, budget.chip_available);
	p.cost = region_cost(region.mode, p.scroll, region.planes);
	p.memory = scroll_memory(p.scroll, visible_w, visible_h, region.planes, region.speed_px);
	p.ok = (p.scroll != ScrollKind::None) || (region.scroll == ScrollKind::None);
	return p;
}

} // namespace eng::scene
