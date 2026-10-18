#pragma once

/// \file hud.hpp
/// **Fachada de `eng::hud`**: un solo include con el modelo de HUD, el contrato de región, el
/// tema, el presentador de franja y la entrada/foco (`HudInput` + `HudFocus`, el HUD como
/// pantalla de GUI: ratón, teclado, joystick, foco por geometría, hover). Diseño canónico en
/// `docs/engine/architecture/HUD_SYSTEM.md`; plan en `docs/guides/roadmap/ROADMAP_HUD.md`.
///
/// Verificación: **HOST-467** (modelo y validadores), **HOST-468** (franja y operaciones),
/// **HOST-469** (entrada: hit-test, click, selección/foco, joystick, `capture_input`, anillo
/// de foco) y **HOST-472** (H5: barra de verbos, tooltip/toast, panel y puente RPG→HUD);
/// demo `302_hud_band` (franja con paleta propia, deltas y presupuesto de 1 campo).

#include <eng/hud/focus.hpp>
#include <eng/hud/input.hpp>
#include <eng/hud/model.hpp>
#include <eng/hud/region.hpp>
#include <eng/hud/strip.hpp>
#include <eng/hud/theme.hpp>
