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

/// **Modo de playfield**: determina qué límites de planos y fetch aplican (y qué `BPLCON0`).
/// No es "cómo se programa" (eso lo hace la etapa `display`), sino el contrato que valida
/// `validate()`.
enum class SceneMode : eng::u8 {
	Standard = 0,     ///< playfield normal (color indexado, N planos)
	Ham = 1,          ///< HAM (OCS/ECS HAM6 = 6 planos; AGA HAM8 = 8)
	Ehb = 2,          ///< extra half-brite (6 planos)
	DualPlayfield = 3 ///< doble playfield (planos repartidos entre los dos PF)
};

/// **Recursos** que una escena planar necesita (plano de recursos del modelo).
struct SceneResources {
	u16 width = 320;   ///< ancho visible (múltiplo de 16)
	u16 height = 256;  ///< filas del display
	u16 rows = 0;      ///< filas lógicas del bitmap (0 = igual a `height`)
	u8 planes = 4;     ///< planos de bitplane
	SceneLayout layout = SceneLayout::Contiguous; ///< disposición de los bitplanes
	SceneMode mode = SceneMode::Standard; ///< modo de playfield (valida planos/fetch por modo)
	u8 buffers = 1;    ///< nº de buffers de display (1/2/3); >1 = doble/triple buffer
	u32 copper_bytes = 4096; ///< capacidad de la copperlist (bytes) reservada en Chip
	u16 first_line = 0x2c; ///< línea de raster donde arranca la ventana visible (Plan)

	// --- Geometría de display (0 = derivar de `width`/`height`) ----------------------
	u16 diwstrt = 0;   ///< DIWSTRT del display (0 = derivar); la etapa `display` lo recibe
	u16 diwstop = 0;   ///< DIWSTOP del display (0 = derivar)
	u16 ddfstrt = 0;   ///< DDFSTRT del fetch (0 = derivar); en lores OCS estándar = 0x0038
	u16 ddfstop = 0;   ///< DDFSTOP del fetch (0 = derivar); en lores OCS estándar = 0x00d0
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
///
/// Los valores por defecto son de **OCS lores**. Fuente: AHRM 3.ª ed., "Playfield Hardware"
/// Tabla 3-14 (límites hw de DDF: `DDFSTRT ≥ 0x18`, `DDFSTOP ≤ 0xD8`, ≤ 25 palabras lores) y
/// Tabla 3-13 (líneas visibles PAL/NTSC); y `docs/reference/amiga/hardware/amiga-chipset-matrix.md`.
struct DisplayLimits {
	/// Nombre del perfil (depuración/informe).
	const char* name = "generic";

	// --- Fetch horizontal (palabras de 16 px en lores) --------------------------------
	u16 fetch_strt_min = 0x0018u; ///< DDFSTRT mínimo admisible por el hardware
	u16 fetch_stop_max = 0x00d8u; ///< DDFSTOP máximo admisible por el hardware
	u16 fetch_strt_std = 0x0038u; ///< DDFSTRT estándar lores (referencia de `width` estándar)
	u16 fetch_stop_std = 0x00d0u; ///< DDFSTOP estándar lores (21 palabras = 336 px de fetch)
	u8 max_fetch_words = 25;      ///< palabras de fetch máximas por línea en lores
	u16 min_width = 16;           ///< ancho mínimo en píxeles de una escena
	u16 max_width = 400;          ///< ancho máximo *fetchable* lores (25 palabras); visible ≤ 368

	// --- Ventana visible --------------------------------------------------------------
	u16 max_visible_pixels = 368; ///< píxeles visibles máximos por línea (blanking hw, lores)
	u16 max_height = 256;         ///< líneas visibles máximas del display (PAL: 256)

	// --- Bitplanes por modo -----------------------------------------------------------
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
	u8 dma_slots_per_word = 1; ///< slots de bus por palabra de bitplane y línea (AGA FMODE ↓)
	u8 fixed_dma_slots = 27;   ///< slots fijos por línea (refresh, sprites, Copper, audio)
	u16 slots_per_line = 227;  ///< slots de bus usables por línea (PAL lores, 7.09 MHz)
};

// ---------------------------------------------------------------------------------------
// Perfiles predefinidos (datos, no funciones).
// ---------------------------------------------------------------------------------------

/// **OCS** (A500/A1000/A2000): 6 planos lores, DPF 3+3, HAM6/EHB6, sin AGA.
inline constexpr DisplayLimits ocs_a500 {
	"OCS/A500",
	0x0018u, 0x00d8u, 0x0038u, 0x00d0u, 25, 16, 400, // fetch / ancho
	368, 256,                                          // visible / alto
	6, 3, 6, 6,                                        // planos: normal/DPF/HAM/EHB
	true, true, true, false,                           // HAM/EHB/DPF sí, AGA no
	3,                                                 // buffers
};

/// **ECS** (A500+/A600/A3000): como OCS en lores (los modos ECS de mayor ancho son
/// producto; aquí el display base comparte límites OCS).
inline constexpr DisplayLimits ecs {
	"ECS",
	0x0018u, 0x00d8u, 0x0038u, 0x00d0u, 25, 16, 400,
	368, 256,
	6, 3, 6, 6,
	true, true, true, false,
	3,
};

/// **AGA** (A1200/A4000/CD32): hasta 8 planos, HAM8, DPF 4+4, 24-bit, FMODE.
inline constexpr DisplayLimits aga_a1200 {
	"AGA/A1200",
	0x0018u, 0x00d8u, 0x0038u, 0x00d0u, 25, 16, 400,
	368, 256,
	8, 4, 8, 6,      // hasta 8 planos; DPF hasta 4+4; HAM8 = 8
	true, true, true, true,
	3,
};

// ---------------------------------------------------------------------------------------
// Validación en tiempo de compilación (consteval) y de ejecución (constexpr).
// ---------------------------------------------------------------------------------------

/// Comprueba `res` contra el perfil `l`. Devuelve el **primer** motivo de rechazo
/// (código/mensaje); `ok()` si la configuración es admisible. Es `constexpr`, así que sirve
/// tanto para `static_assert` (config conocida en compilación) como para el chequeo runtime.
///
/// Códigos: 1 ancho no múltiplo de 16 · 2/3 ancho fuera de rango · 4 alto · 5 planos ·
/// 6 filas lógicas · 7 buffers · 8 modo no soportado · 9 planos del modo · 10 DDF incoherente.
[[nodiscard]] constexpr ConfigError validate(const SceneResources& res,
					     const DisplayLimits& l) {
	// 1) Anchura: múltiplo de la palabra de fetch (16 px), dentro del rango fetchable/visible.
	if (res.width == 0u || (res.width % 16u) != 0u) {
		return {1u, "width debe ser multiplo de 16 (palabra de fetch lores)"};
	}
	if (res.width < l.min_width) {
		return {2u, "width por debajo del minimo del backend"};
	}
	if (res.width > l.max_width) {
		return {3u, "width por encima del maximo fetchable del backend (OCS lores: 400)"};
	}
	if (res.width > l.max_visible_pixels) {
		return {3u, "width supera los pixeles visibles del backend (blanking, OCS lores: 368)"};
	}

	// 2) Altura.
	if (res.height == 0u || res.height > l.max_height) {
		return {4u, "height fuera de rango del backend"};
	}

	// 3) Filas lógicas: no pueden superar la altura física del display (el bitmap puede ser
	//    más bajo: `rows <= height`, p. ej. para cuadruplicado).
	if (res.rows != 0u && res.rows > res.height) {
		return {6u, "rows (filas logicas) no puede superar height"};
	}

	// 4) Planos: normal y por modo (HAM/EHB/DPF).
	if (res.planes == 0u || res.planes > l.max_planes) {
		return {5u, "nº de planos por encima del maximo del backend"};
	}
	switch (res.mode) {
		case SceneMode::Standard:
			break;
		case SceneMode::Ham:
			if (!l.supports_ham) {
				return {8u, "el backend no soporta HAM"};
			}
			if (res.planes > l.max_planes_ham) {
				return {9u, "nº de planos por encima del maximo de HAM del backend"};
			}
			break;
		case SceneMode::Ehb:
			if (!l.supports_ehb) {
				return {8u, "el backend no soporta EHB"};
			}
			if (res.planes > l.max_planes_ehb) {
				return {9u, "nº de planos por encima del maximo de EHB del backend"};
			}
			break;
		case SceneMode::DualPlayfield:
			if (!l.supports_dpf) {
				return {8u, "el backend no soporta dual playfield"};
			}
			if (res.planes > l.max_planes_dpf) {
				return {9u, "nº de planos por encima del maximo de dual playfield del backend"};
			}
			break;
	}
	// Cualquier plano > 6 exige AGA (capacidades extendidas).
	if (res.planes > 6u && !l.supports_aga) {
		return {5u, "nº de planos > 6 requiere AGA"};
	}

	// 5) DDF coherente con el ancho (solo si se especifica; 0 = derivar). El fetch en lores
	//    es `2 + (ddfstop - ddfstrt)/2 * 2 = 2 + (ddfstop-ddfstrt)` palabras de 2 bytes, es
	//    decir palabras = 1 + (ddfstop - ddfstrt)/2 * ... ; la comprobación práctica: el fetch
	//    debe cubrir el ancho pedido y respetar los límites hw de DDFSTRT/DDFSTOP.
	if (res.ddfstrt != 0u || res.ddfstop != 0u) {
		if (res.ddfstrt < l.fetch_strt_min || res.ddfstop > l.fetch_stop_max ||
		    res.ddfstop <= res.ddfstrt) {
			return {10u, "DDFSTRT/DDFSTOP fuera de los limites hw del backend"};
		}
		// Palabras de fetch lores = (ddfstop - ddfstrt)/8 + 1  (paso de DDF = 8 bytes = 1
		// palabra de 16 px). Estándar 0x38..0xD0 -> (0xD0-0x38)/8+1 = 20 palabras = 320 px;
		// máximo hw 0x18..0xD8 -> 25 palabras (AHRM Tabla 3-14).
		const u16 fetch_words =
			static_cast<u16>((static_cast<u16>(res.ddfstop - res.ddfstrt) / 8u) + 1u);
		if (fetch_words > l.max_fetch_words) {
			return {10u, "DDFSTRT/DDFSTOP superan las palabras de fetch maximas del backend"};
		}
		if (static_cast<u32>(fetch_words) * 16u < res.width) {
			return {10u, "DDFSTRT/DDFSTOP no cubren el ancho pedido"};
		}
	}

	// 6) Buffers de display.
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

// ---------------------------------------------------------------------------------------
// Geometría de display derivada de los recursos.
// ---------------------------------------------------------------------------------------

/// **Geometría de display** (DIWSTRT/DIWSTOP/DDFSTRT/DDFSTOP): ventana visible y rango de
/// fetch de bitplanes. Es data de composición derivada de `SceneResources`.
struct DisplayGeometry {
	u16 diwstrt = 0x2c81; ///< DIWSTRT (ventana visible, esquina superior)
	u16 diwstop = 0x2cc1; ///< DIWSTOP (ventana visible, esquina inferior)
	u16 ddfstrt = 0x0038; ///< DDFSTRT (inicio del fetch de bitplanes)
	u16 ddfstop = 0x00d0; ///< DDFSTOP (fin del fetch de bitplanes)
};

/// PAL lowres **320×256** con *fetch* estándar (40 B/fila): geometría habitual.
inline constexpr DisplayGeometry kPal320x256 {0x2c81, 0x2cc1, 0x0038, 0x00d0};

/// **Geometría de display** coherente con los recursos (DIW/DDF). Si `res` la especifica
/// (campos != 0) se respeta; si no, se deriva del ancho estándar lores: DIW `0x2c81/0x2cc1`
/// y DDF `0x0038` + palabras de fetch del ancho. La consumen las etapas de composición.
[[nodiscard]] constexpr DisplayGeometry geometry_for(const SceneResources& res) {
	DisplayGeometry g {};
	g.diwstrt = (res.diwstrt != 0u) ? res.diwstrt : 0x2c81u;
	g.diwstop = (res.diwstop != 0u) ? res.diwstop : 0x2cc1u;
	if (res.ddfstrt != 0u || res.ddfstop != 0u) {
		g.ddfstrt = res.ddfstrt;
		g.ddfstop = res.ddfstop;
	} else {
		// Estándar lores: `DDFSTRT = 0x38`, y `DDFSTOP` tal que cubra `width`. Palabras de
		// fetch = (ddfstop - ddfstrt)/8 + 1 (paso de DDF = 8 B = 1 palabra de 16 px). Para
		// 320 px -> (0xD0-0x38)/8+1 = 20 palabras (el estándar). Se acota al máximo hw 0xD8.
		g.ddfstrt = 0x0038u;
		const u16 words = static_cast<u16>((res.width + 15u) / 16u);
		u16 stop = static_cast<u16>(g.ddfstrt + 8u * (words - 1u));
		if (stop > 0x00d8u) {
			stop = 0x00d8u;
		}
		g.ddfstop = stop;
	}
	return g;
}

/// **Coste de bus** de un playfield (informativo; **no** es validez). El nº de planos no
/// cambia el ancho de fetch, pero cada plano consume slots de Chip RAM por línea que
/// compiten con CPU/Blitter/Copper: a 6 planos lores ya se usa ~65 % del bus y queda ~35 %
/// para la CPU (fuente: `amiga-bootcamp/01_hardware/common/dma_architecture.md`).
struct DmaCost {
	u16 fetch_words = 0;   ///< palabras de fetch por línea y plano
	u32 bitplane_slots = 0;///< slots de bus consumidos por los bitplanes (por línea)
	u16 cpu_slots = 0;     ///< slots restantes para CPU/Blitter (por línea)
};

/// Calcula el coste de bus de `res` bajo `l` (ver `DmaCost`). `planes` = planos de bitplane.
[[nodiscard]] constexpr DmaCost dma_cost(const SceneResources& res, const DisplayLimits& l) {
	const DisplayGeometry g = geometry_for(res);
	const u16 fetch_words = static_cast<u16>(
		(static_cast<u16>(g.ddfstop - g.ddfstrt) / 8u) + 1u);
	const u32 bp = static_cast<u32>(fetch_words) * res.planes * l.dma_slots_per_word;
	const u16 total = l.slots_per_line;
	const u16 used = static_cast<u16>(bp + l.fixed_dma_slots);
	return DmaCost {fetch_words, bp, static_cast<u16>(used < total ? total - used : 0u)};
}

} // namespace eng::graphics::scene
