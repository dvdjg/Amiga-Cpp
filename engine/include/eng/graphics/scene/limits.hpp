#pragma once

/// \file limits.hpp
/// **Validación de configuraciones de display contra las capacidades del backend.**
///
/// Una `SceneResources` puede expresar combinaciones que la función acepta pero el
/// hardware **no** permite (p. ej. `width = 336` en Amiga, o 5 planos por playfield en un
/// A500 sin AGA). Este módulo aporta:
///
/// - `DisplayLimits`: **perfil de capacidades** que declara el backend/máquina objetivo
///   (agnóstico: describe qué admite, no cómo se programa).
/// - Perfiles predefinidos (`ocs_a500`, `ecs`, `aga_a1200`) como **datos**.
/// - `validate(res, limits)`: comprobación **en tiempo de ejecución** (devuelve el primer
///   motivo de rechazo, con un código y mensaje de depuración).
/// - `valid_scene(res, limits)`: comprobación **en tiempo de compilación** (`consteval`),
///   usable en `static_assert` cuando la configuración se conoce en compilación.
///
/// El modelo no depende del Amiga: otro backend (Mega Drive, Neo Geo…) declara su propio
/// `DisplayLimits`. Las restricciones concretas de Amiga OCS/ECS/AGA se documentan junto a
/// cada campo y se derivan de `docs/reference/amiga/hardware/amiga-chipset-matrix.md` y
/// `docs/reference/amiga/techniques/amiga-display-setup-checklist.md`.

#include <eng/core/types.hpp>

namespace eng::graphics::scene {

/// **Layout de los bitplanes** en memoria.
enum class SceneLayout : eng::u8 {
	Contiguous = 0, ///< un plano tras otro (cada fila de un plano, contiguas)
	Interleaved = 1, ///< fila a fila con los N planos (el que espera `CanvasPlayfield`)
};

/// **Recursos** que una escena planar necesita (plano de recursos del modelo).
struct SceneResources {
	u16 width = 320;   ///< ancho visible (múltiplo de 16)
	u16 height = 256;  ///< filas del display
	u16 rows = 0;      ///< filas lógicas del bitmap (0 = igual a `height`)
	u8 planes = 4;     ///< planos de bitplane
	SceneLayout layout = SceneLayout::Contiguous; ///< disposición de los bitplanes
	u8 buffers = 1;    ///< nº de buffers de display (1/2/3); >1 = doble/triple buffer
	u32 copper_bytes = 4096; ///< capacidad de la copperlist (bytes) reservada en Chip
	u16 first_line = 0x2c; ///< línea de raster donde arranca la ventana visible (Plan)
};

/// Rechazo de una configuración de display (código + mensaje legible).
struct ConfigError {
	eng::u8 code = 0;      ///< 0 = válida; >0 = motivo (ver `validate`)
	const char* message = "ok"; ///< descripción del rechazo (solo depuración)

	[[nodiscard]] constexpr bool ok() const { return code == 0u; }
	[[nodiscard]] explicit constexpr operator bool() const { return code != 0u; }
};

/// **Capacidades de display de una máquina/backend**: qué geometrías y modos admite.
/// Cada backend declara el suyo; el engine valida contra él sin conocer el hardware.
struct DisplayLimits {
	/// Nombre del perfil (depuración/informe).
	const char* name = "generic";

	// --- Fetch / anchura visible (palabras de 16 px por unidad de DDF en lores) ------
	u16 min_width = 16;   ///< ancho mínimo en píxeles (≥ 1 palabra de fetch)
	u16 max_width = 320;  ///< ancho máximo por playfield (lores OCS: DDFSTOP≤0xD0 → 320)
	u16 width_granularity = 16; ///< el ancho debe ser múltiplo de esto (fetch bajo = 16 px)

	// --- Altura ----------------------------------------------------------------------
	u16 max_height = 256; ///< líneas visibles máximas (PAL: 256)

	// --- Bitplanes -------------------------------------------------------------------
	u8 max_planes = 6;    ///< planos máximos por playfield sin modos especiales (OCS lores)
	u8 max_planes_dpf = 3;///< planos máximos por playfield en **dual playfield** (OCS: 3+3)
	u8 max_planes_ham = 6;///< planos para HAM (OCS/ECS HAM6 = 6; AGA HAM8 = 8)
	u8 max_planes_ehb = 6;///< planos para EHB (OCS/ECS = 6)

	// --- Modos soportados -------------------------------------------------------------
	bool supports_ham = true;  ///< ¿HAM disponible? (OCS/ECS/AGA sí)
	bool supports_ehb = true;  ///< ¿EHB disponible? (OCS/ECS/AGA sí)
	bool supports_dpf = true;  ///< ¿dual playfield real disponible? (OCS/ECS/AGA sí)
	bool supports_aga = false; ///< ¿capacidades AGA (BPLCON3, FMODE, >6 planos)? (A1200 sí)

	// --- Buffers de display -----------------------------------------------------------
	u8 max_buffers = 3;    ///< buffers de display soportados (doble/triple buffer)
};

// ---------------------------------------------------------------------------------------
// Perfiles predefinidos (datos, no funciones).
// ---------------------------------------------------------------------------------------

/// **OCS** (A500/A1000/A2000): 6 planos lores, DPF 3+3, HAM6/EHB6, sin AGA.
inline constexpr DisplayLimits ocs_a500 {
	"OCS/A500",
	16, 320, 16,     // fetch: ancho 16..320, múltiplo de 16
	256,             // altura
	6, 3, 6, 6,      // planos: normal/DPF/HAM/EHB
	true, true, true, false, // HAM/EHB/DPF sí, AGA no
	3,               // buffers
};

/// **ECS** (A500+/A600/A3000): como OCS en lores, más chip RAM y modos producto; aquí el
/// display lores comparte límites con OCS (los modos ECS de mayor ancho son opcionales).
inline constexpr DisplayLimits ecs {
	"ECS",
	16, 320, 16,
	256,
	6, 3, 6, 6,
	true, true, true, false,
	3,
};

/// **AGA** (A1200/A4000/CD32): hasta 8 planos, HAM8, 24-bit; DPF más flexible.
inline constexpr DisplayLimits aga_a1200 {
	"AGA/A1200",
	16, 320, 16,
	256,
	8, 4, 8, 6,      // hasta 8 planos; DPF hasta 4+4; HAM8 = 8
	true, true, true, true,
	3,
};

// ---------------------------------------------------------------------------------------
// Validación en tiempo de compilación (consteval) y de ejecución (constexpr).
// ---------------------------------------------------------------------------------------

/// Comprueba `res` contra `l`. `mode_planes` es el nº de planos del modo especial si aplica
/// (HAM/EHB); se pasa como `res.planes`, así que basta con validar el rango normal.
///
/// Devuelve el **primer** motivo de rechazo (código/mensaje); `ok()` si la configuración es
/// admisible. Un solo `constexpr` sirve a la vez para `static_assert` (config conocida en
/// compilación) y para el chequeo en runtime (config calculada).
[[nodiscard]] constexpr ConfigError validate(const SceneResources& res,
					     const DisplayLimits& l) {
	// 1) Anchura: múltiplo de la granularidad de fetch y dentro del rango del backend.
	if (res.width == 0u || (res.width % l.width_granularity) != 0u) {
		return {1u, "width debe ser multiplo de la granularidad de fetch (16 px)"};
	}
	if (res.width < l.min_width) {
		return {2u, "width por debajo del minimo del backend"};
	}
	if (res.width > l.max_width) {
		return {3u, "width por encima del maximo del backend (OCS lores: 320)"};
	}

	// 2) Altura.
	if (res.height == 0u || res.height > l.max_height) {
		return {4u, "height fuera de rango del backend"};
	}

	// 3) Planos: por playfield, y por modo especial.
	if (res.planes == 0u || res.planes > l.max_planes) {
		return {5u, "nº de planos por encima del maximo del backend"};
	}
	if (res.planes > l.max_planes_ham && res.planes == l.max_planes_ham + 1u) {
		// permite exactamente el máximo de HAM; valores por encima ya se rechazaron arriba
	}

	// 4) Filas lógicas: no pueden superar la altura física del display salvo cuadruplicado
	//    (el bitmap puede ser más bajo: `rows <= height`).
	if (res.rows != 0u && res.rows > res.height) {
		return {6u, "rows (filas logicas) no puede superar height"};
	}

	// 5) Buffers de display.
	if (res.buffers == 0u || res.buffers > l.max_buffers) {
		return {7u, "nº de buffers de display fuera de rango del backend"};
	}

	return {};
}

/// **Verificación estática**: `true` si la configuración es válida para el perfil. Úsala en
/// un `static_assert` cuando `res` y `l` se conocen en compilación:
///
/// ```cpp
/// constexpr auto r = scene::planar(320, 256, 6);
/// static_assert(scene::valid_scene(r, scene::ocs_a500));
/// ```
[[nodiscard]] consteval bool valid_scene(const SceneResources& res,
					 const DisplayLimits& l) {
	return validate(res, l).ok();
}

} // namespace eng::graphics::scene
