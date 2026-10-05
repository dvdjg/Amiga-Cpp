#pragma once

/// \file zx0.hpp
/// **Alias histórico** del descompresor ZX0, ahora **etapa genérica de recursos** en
/// `eng/res/zx0.hpp` (`eng::res::zx0`, `ROADMAP_RESOURCES.md` R6.5). Se conserva para el codec de
/// audio (`eng::audio::zx0`); el código nuevo debe usar `eng::res::zx0`.

#include <eng/res/zx0.hpp>

namespace eng::audio {
namespace zx0 = ::eng::res::zx0;
} // namespace eng::audio
