#pragma once

/// \file amiga_minimal.hpp
/// Cabecera-paraguas de compatibilidad. El backend Amiga canónico vive en
/// `eng/platform/amiga/backend.hpp` (`eng::amiga::AmigaBackend`). Mantén esta ruta
/// solo mientras existan consumidores sin migrar; el nombre canónico es `AmigaBackend`.
/// Ver docs/engine/architecture/PLATFORM_LAYERS.md.
#include <eng/platform/amiga/backend.hpp>
