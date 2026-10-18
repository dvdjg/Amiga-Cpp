#pragma once

/// \file region.hpp
/// **Contrato de región del HUD** (`eng::hud`): colocación (`Band`/`Overlay`/`Screen`), banda,
/// presupuesto y validadores puros de *setup*. La región **no dibuja**: el compositor de escena
/// la materializa con `scene::LayerPlan` + `CanvasPlayfield` + `ModeSwitchZone` (diseño
/// `docs/engine/architecture/HUD_SYSTEM.md` §4). El HUD nunca escribe registros del chipset.
///
/// Verificación: validadores cubiertos por **HOST-467**; región `Band` en hardware con la demo
/// `302_hud_band` (franja de 2 planos con paleta propia en la línea 208).

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>

namespace eng::hud {

struct HudTheme; // definido en theme.hpp; la región lo referencia como observador

/// Colocación de una región de HUD (ver `HUD_SYSTEM.md` §4.1).
enum class HudPlacement : eng::u8 {
	Band = 0, ///< franja propia (planos/paleta opcionales; `CanvasPlayfield`)
	Overlay,  ///< sobre el campo de juego visible (con política de restauración)
	Screen,   ///< panel modal a pantalla completa (sustituye o cubre la escena)
};

/// Política de restauración de un overlay: quién devuelve el fondo que el HUD tapa.
enum class HudOverlayRestore : eng::u8 {
	GameRedraws = 0, ///< el juego repinta su viewport cada frame (sin *backing*)
	Backing,         ///< el HUD guarda y restaura los píxeles que tapa (coste declarado)
};

/// Líneas de display del modo canónico PAL 320×256 (el corte de banda se valida contra él).
inline constexpr eng::u16 kHudDisplayLines = 256u;

/// Planos máximos del chipset OCS/ECS (6 = EHB). Un tramo con más planos no existe.
inline constexpr eng::u8 kHudMaxPlanes = 6u;

/// Banda del HUD: tramo vertical con geometría de vídeo propia.
struct HudBand {
	eng::u16 top = 0u;         ///< primera línea de display de la franja
	eng::u16 height = 0u;      ///< alto en líneas (0 = sin banda; inválido para `Band`)
	eng::u8 planes = 0u;       ///< planos del tramo (0 = hereda la profundidad del campo)
	bool own_palette = false;  ///< true si la franja emite su propia paleta en el corte
};

/// Presupuesto por región: lo que el presentador puede gastar por frame. Si se excede, se aplica
/// la escalera de degradación (`HUD_SYSTEM.md` §6.6); el *hit-test* y los valores nunca se degradan.
struct HudBudget {
	eng::u16 max_draw_ops = 0u;              ///< operaciones de dibujo por frame
	eng::u16 max_blits_per_frame = 0u;       ///< jobs de Blitter por frame
	eng::u16 max_copper_moves_per_line = 0u; ///< movimientos de Copper por línea en el corte
	eng::u16 max_backing_bytes = 0u;         ///< bytes de *backing* de overlay (Chip)
	eng::u8 sprite_channel_mask = 0u;        ///< bits SPR0..SPR7 reservados (0 = ninguno)
	eng::u8 max_bobs = 0u;                   ///< BOBs de overlay por frame
};

/// Especificación de una región de HUD. Es un descriptor puro: el *planner* de escena lo compila
/// a capas y canales; el HUD no lo interpreta como orden de dibujo directo.
struct HudRegionSpec {
	HudPlacement placement = HudPlacement::Band; ///< colocación
	HudBand band {};                             ///< banda (solo `Band`)
	HudOverlayRestore restore = HudOverlayRestore::GameRedraws; ///< restauración (solo `Overlay`)
	eng::u8 z = 0u;                              ///< orden entre overlays; mayor = delante
	bool capture_input = false;                  ///< consume la entrada de la región
	HudBudget budget {};                         ///< presupuesto por frame
	eng::Ref<const HudTheme> theme {};           ///< tema (observador; la app es la dueña)
};

/// Valida una banda: alto positivo, corte dentro del display y planos representables.
[[nodiscard]] constexpr bool band_valid(const HudBand& band) noexcept {
	const eng::u32 end = static_cast<eng::u32>(band.top) + static_cast<eng::u32>(band.height);
	return band.height != 0u && end <= static_cast<eng::u32>(kHudDisplayLines) &&
	       band.planes <= kHudMaxPlanes;
}

/// Valida una región completa: coherencia entre colocación, banda y restauración. Una región
/// inválida no debe llegar al *setup* (se rechaza antes de tocar hardware).
[[nodiscard]] constexpr bool region_valid(const HudRegionSpec& spec) noexcept {
	switch (spec.placement) {
	case HudPlacement::Band:
		return band_valid(spec.band);
	case HudPlacement::Overlay:
		return spec.band.height == 0u &&
		       (spec.restore == HudOverlayRestore::GameRedraws || spec.budget.max_backing_bytes != 0u);
	case HudPlacement::Screen:
		return spec.band.height == 0u;
	}
	return false;
}

} // namespace eng::hud
