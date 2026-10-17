#pragma once

/// \file hud.hpp
/// **Fachada de `eng::hud`**: un solo include con el modelo de HUD, el contrato de región, el
/// tema y el presentador de franja. Diseño canónico en
/// `docs/engine/architecture/HUD_SYSTEM.md`; plan en `docs/guides/roadmap/ROADMAP_HUD.md`.
///
/// Verificación: **HOST-467** (modelo y validadores) y **HOST-468** (franja y operaciones).
/// **NO VERIFICADA** por demo: la primera demo del HUD (`302_hud_band`) llega en la fase H1.

#include <eng/hud/model.hpp>
#include <eng/hud/region.hpp>
#include <eng/hud/strip.hpp>
#include <eng/hud/theme.hpp>
