#pragma once

/// \file hud.hpp
/// **Fachada de `eng::hud`**: un solo include con el modelo de HUD, el contrato de región, el
/// tema, el presentador de franja y la entrada (`HudInput`). Diseño canónico en
/// `docs/engine/architecture/HUD_SYSTEM.md`; plan en `docs/guides/roadmap/ROADMAP_HUD.md`.
///
/// Verificación: **HOST-467** (modelo y validadores), **HOST-468** (franja y operaciones) y
/// **HOST-469** (entrada: hit-test, click, selección/foco, consumo por `capture_input`);
/// demo `302_hud_band` (franja con paleta propia, deltas y presupuesto de 1 campo).

#include <eng/hud/input.hpp>
#include <eng/hud/model.hpp>
#include <eng/hud/region.hpp>
#include <eng/hud/strip.hpp>
#include <eng/hud/theme.hpp>
