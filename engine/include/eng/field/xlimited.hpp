#pragma once

/// \file xlimited.hpp
/// Cabecera de familia de X-Limited/corkscrew: reúne la base (policies, `XlimitedConfigT`),
/// el playfield (`XLimitedPlayfield`) y los compositores de display. Los consumidores
/// pueden seguir incluyendo este header sin cambios; quien solo necesite una parte puede
/// incluir `xlimited_base.hpp` / `xlimited_playfield.hpp` / `xlimited_composer.hpp`.
///
/// ```text
///   xlimited_base.hpp        policies + XlimitedConfigT + detail
///         │
///         ▼
///   xlimited_playfield.hpp   XLimitedPlayfield (corkscrew 8-way)
///         │
///         ▼
///   xlimited_composer.hpp    XlimitedDisplayComposer / Dual (BPLCON/DIW/DDF)
/// ```

#include <eng/field/xlimited_base.hpp>
#include <eng/field/xlimited_composer.hpp>
#include <eng/field/xlimited_playfield.hpp>
