#pragma once

/// \file palette32.hpp
/// **Paleta de valor de 32 colores RGB444**: el almacenamiento de los 32 registros
/// fisicos de paleta (`COLOR00..COLOR31`). `PaletteWords` es la vista de dominio; este
/// tipo es el dueno de los datos. Es la base de una escena EHB (32 colores fisicos,
/// 64 percibidos por el sexto bitplane), de un fundido o de un ciclo de paleta.
///
/// Es un tipo de valor del engine, independiente de cualquier driver grafico: los
/// efectos (`PaletteTransitionEffect`, `PaletteCycleEffect`) y el tile scroll lo usan
/// sin depender de una escena concreta.
///
/// ```text
///   Palette32 (DUEÑO, 32 × u16 RGB444)  ──►  PaletteWords (vista de dominio, sin cast)
///     color[0..31]                              { ptr, 32 }
///   la usan: escena EHB · fundido (PaletteTransitionEffect) · ciclo (PaletteCycleEffect) · tile scroll
///   Palette32Zone { line, *palette }: cambio de paleta en una línea (zona de Copper)
/// ```

#include <eng/core/domains.hpp>
#include <eng/core/types.hpp>

namespace eng {

/// 32 colores RGB444 (los registros fisicos de paleta del Amiga).
struct Palette32 {
	u16 color[32] {};

	/// Vista de dominio: permite pasar un `Palette32` a cualquier API que pida
	/// `PaletteWords` (p. ej. `emit_palette`), sin casts.
	constexpr operator PaletteWords() const noexcept { return PaletteWords {color, 32u}; }

	/// Vista de dominio de los 32 colores (para APIs que piden `PaletteWords`).
	[[nodiscard]] constexpr PaletteWords words() const noexcept {
		return PaletteWords {color, 32u};
	}
};

/// Paleta totalmente negra, compartida: origen/destino natural de un fundido.
inline constexpr Palette32 kBlackPalette {};

/// Cambio de paleta en una linea concreta (zona Copper). `line` usa el mismo espacio que
/// `copper::Scheduler::wait_line`. `palette` debe apuntar a datos vivos durante la
/// construccion de la copperlist (despues el Copper ya tiene copia de los 32 valores).
struct Palette32Zone {
	u8 line = 0;
	const Palette32* palette = nullptr;
};

} // namespace eng
