#pragma once

/// \file coherence.hpp
/// **Validadores de coherencia de configuración** (`eng::setup`): comprueban, **en setup**,
/// que los valores que la demo programará en el hardware son mutuamente consistentes. Son
/// funciones **puras** (sin hardware, sin heap, `constexpr`): el mismo código sirve para el
/// chequeo en la demo, para `static_assert` y para los tests host.
///
/// Nacen de dos fallos reales de la demo 066 que pasaron **en silencio** hasta verse en
/// pantalla:
///
/// - `BPLCON0` con `BPU = 5` cuando la demo emitía punteros de 4 planos: el 5.º plano leía
///   `BPL5PT` (basura) y aparecían colores 16-31 espurios.
/// - `execute_frame_plan_async` devolvía `false` (el slot de IRQ de blit ya estaba ocupado por
///   el bombeo de fondo del `Engine`) y el plan no se ejecutaba.
///
/// El sistema completo (qué se puede exigir en compilación, qué en setup y qué en tests)
/// está en `docs/engine/architecture/COHERENCIA_DE_SETUP.md`.
///
/// ```text
///   capa 1 (compilación)   tipos + [[nodiscard]] + -Werror=unused-result + static_assert
///   capa 2 (setup)         ESTE fichero: regiones, BPLCON0, orden de WAITs del Copper
///   capa 3 (tests host)    los casos históricos como casos del test (441)
/// ```

#include <eng/core/types/types.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/composition/limits.hpp>
#include <eng/graphics/sprite_limits.hpp>

namespace eng::setup {

/// Resultado de un chequeo: `ok()` o el **primer** problema con su contexto. Mismo patrón que
/// `composition::ConfigError` (código + mensaje para depuración), con `index`/`expected`/`actual`
/// para que un host (o el `mark_failed`) pueda diagnosticar sin recompilar.
struct CheckResult {
	eng::u8 code = 0u;              ///< 0 = coherente; >0 = motivo (ver cada validador)
	const char* message = "ok";     ///< descripción del rechazo (solo depuración)
	eng::u16 index = 0u;            ///< elemento culpable (región, palabra de Copper…)
	eng::u32 expected = 0u;         ///< valor que debía tener (contexto del fallo)
	eng::u32 actual = 0u;           ///< valor encontrado

	[[nodiscard]] constexpr bool ok() const noexcept { return code == 0u; }
	[[nodiscard]] explicit constexpr operator bool() const noexcept { return code != 0u; }
};

// =====================================================================================
// Regiones de memoria: solape parcial y límites del banco
// =====================================================================================

/// Una región con nombre del mapa de memoria de la demo (bloque de arena, copperlist, hoja de
/// BOB…). `base` es una dirección **absoluta** de Amiga (los bloques de arena la dan).
struct Region {
	const char* name = ""; ///< nombre legible (diagnóstico)
	eng::u32 base = 0u;    ///< dirección absoluta del inicio
	eng::u32 bytes = 0u;   ///< tamaño en bytes (0 = región vacía, no cubre nada)
};

/// Mapa de regiones con **detección de solape parcial** y de **límites del banco**.
///
/// La regla de solape es la que hace útil al mapa con jerarquía padre-hijo: dos regiones
/// pueden **contenerse** (una hoja de BOB dentro del bloque de hojas, el bloque dentro del
/// banco), pero **no** pueden solaparse **parcialmente** (una hoja que rebasa su bloque e
/// invade el siguiente, el copper encima de los bitplanes). Es el chequeo que convierte un
/// error de aritmética de offsets (en silencio hasta que se ve basura en pantalla) en un
/// fallo de setup con nombre y direcciones.
template <eng::u16 MaxRegions>
class RegionTable {
public:
	/// Añade una región. Devuelve `false` si la tabla está llena (no registra nada).
	bool add(const char* name, eng::u32 base, eng::u32 bytes) noexcept {
		if (m_count >= MaxRegions) {
			return false;
		}
		m_regions[m_count] = Region {name, base, bytes};
		++m_count;
		return true;
	}

	[[nodiscard]] constexpr eng::u16 count() const noexcept { return m_count; }
	[[nodiscard]] constexpr const Region& region(eng::u16 i) const noexcept { return m_regions[i]; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_count >= MaxRegions; }

	/// Primer **solape parcial** entre regiones (o desbordamiento de dirección `u32`).
	/// Códigos: 1 = solape parcial · 3 = `base + bytes` desborda 32 bits.
	[[nodiscard]] constexpr CheckResult validate() const noexcept {
		for (eng::u16 i = 0u; i < m_count; ++i) {
			const Region& a = m_regions[i];
			if (a.bytes == 0u) {
				continue;
			}
			if (a.base + a.bytes < a.base) {
				return {3u, "region desborda el espacio de direcciones", i, 0u, a.base};
			}
			for (eng::u16 j = static_cast<eng::u16>(i + 1u); j < m_count; ++j) {
				const Region& b = m_regions[j];
				if (b.bytes == 0u) {
					continue;
				}
				const eng::u32 start = (a.base > b.base) ? a.base : b.base;
				const eng::u32 a_end = a.base + a.bytes;
				const eng::u32 b_end = b.base + b.bytes;
				const eng::u32 end = (a_end < b_end) ? a_end : b_end;
				if (start >= end) {
					continue; // disjuntas
				}
				// Intersección no vacía: ¿es una contención completa? (permitida)
				const eng::u32 inter = end - start;
				if (inter == a.bytes || inter == b.bytes) {
					continue; // una contiene a la otra (padre-hijo): coherente
				}
				// `index` empaqueta el par culpable: bits 8-15 = primera región (i),
				// bits 0-7 = segunda (j). `expected`/`actual` = bases de ambas.
				return {1u, "solape parcial entre regiones",
					static_cast<eng::u16>((i << 8u) | (j & 0xffu)), a.base, b.base};
			}
		}
		return {};
	}

	/// Como `validate()`, y además exige que **todas** las regiones vivan dentro del banco
	/// `[window_base, window_base + window_bytes)`. Códigos: 2 = fuera del banco.
	[[nodiscard]] constexpr CheckResult validate_within(eng::u32 window_base,
							    eng::u32 window_bytes) const noexcept {
		const CheckResult overlap = validate();
		if (!overlap.ok()) {
			return overlap;
		}
		const eng::u32 window_end = window_base + window_bytes;
		for (eng::u16 i = 0u; i < m_count; ++i) {
			const Region& r = m_regions[i];
			if (r.bytes == 0u) {
				continue;
			}
			if (r.base < window_base || r.base + r.bytes > window_end) {
				return {2u, "region fuera del banco de memoria", i, window_base, r.base};
			}
		}
		return {};
	}

private:
	Region m_regions[MaxRegions] {};
	eng::u16 m_count = 0u;
};

// =====================================================================================
// BPLCON0 contra los planos realmente emitidos
// =====================================================================================

/// Bits de `BPLCON0` que importan aquí (AHRM 3.ª, "BPLCON0 - Bitplane Control").
inline constexpr eng::u16 kBplcon0BpuShift = 12u;  ///< BPU[2:0] = planos - 1… (nº de planos)
inline constexpr eng::u16 kBplcon0Color = 0x0200u; ///< bit 9: COLOR_ON (compuesto A1000)
inline constexpr eng::u16 kBplcon0Dblpf = 0x0400u; ///< bit 10: doble playfield
inline constexpr eng::u16 kBplcon0Ham = 0x0800u;   ///< bit 11: HAM (HOMOD)

/// Comprueba que el `BPLCON0` **casa con los planos que la lista emite**: BPU = nº de planos
/// (el fallo del 5.º plano fantasma), bit COLOR presente (divergencia silenciosa en RGB pero
/// incoherente con el canon del engine: solo el A1000 lo necesita para compuesto, AHRM
/// §"Enable Color Display"), HAM solo con 6 planos, DBLPF solo con nº par.
/// Códigos: 1 BPU≠planos · 2 falta COLOR · 3 HAM≠6 · 4 DBLPF impar.
[[nodiscard]] constexpr CheckResult check_bplcon0(eng::u16 bplcon0, eng::u8 planes,
						  bool require_color = true) noexcept {
	const eng::u16 bpu = static_cast<eng::u16>((bplcon0 >> kBplcon0BpuShift) & 0x7u);
	if (bpu != static_cast<eng::u16>(planes & 0x7u)) {
		return {1u, "BPU de BPLCON0 no casa con los planos emitidos", 0u,
			static_cast<eng::u32>(planes), static_cast<eng::u32>(bpu)};
	}
	if (require_color && (bplcon0 & kBplcon0Color) == 0u) {
		return {2u, "BPLCON0 sin bit COLOR (incoherente con el canon del engine)", 0u,
			static_cast<eng::u32>(kBplcon0Color), static_cast<eng::u32>(bplcon0)};
	}
	if ((bplcon0 & kBplcon0Ham) != 0u && planes != 6u) {
		return {3u, "HAM exige 6 planos (HAM6)", 0u, 6u, static_cast<eng::u32>(planes)};
	}
	if ((bplcon0 & kBplcon0Dblpf) != 0u && (planes & 1u) != 0u) {
		return {4u, "doble playfield exige nº par de planos", 0u, 0u,
			static_cast<eng::u32>(planes)};
	}
	return {};
}

/// Coherencia de `BPLCON0` **con el modo de escena**: EHB exige 6 planos; HAM exige 6 y el
/// bit HAM. (El resto de límites de modo los valida `composition::validate`.)
/// Códigos: 5 EHB≠6 · 6 HAM sin bit HAM.
[[nodiscard]] constexpr CheckResult check_bplcon0_mode(
	eng::u16 bplcon0, eng::graphics::composition::SceneMode mode, eng::u8 planes) noexcept {
	using eng::graphics::composition::SceneMode;
	switch (mode) {
		case SceneMode::Ehb:
			if (planes != 6u) {
				return {5u, "EHB exige 6 planos", 0u, 6u, static_cast<eng::u32>(planes)};
			}
			break;
		case SceneMode::Ham:
			if (planes != 6u) {
				return {5u, "HAM exige 6 planos", 0u, 6u, static_cast<eng::u32>(planes)};
			}
			if ((bplcon0 & kBplcon0Ham) == 0u) {
				return {6u, "modo HAM sin bit HAM en BPLCON0", 0u,
					static_cast<eng::u32>(kBplcon0Ham), static_cast<eng::u32>(bplcon0)};
			}
			break;
		default:
			break;
	}
	return {};
}

// =====================================================================================
// Parcheo de SPRxDATA/DATB a media línea contra el nº de planos del display
// =====================================================================================

/// El parcheo de `SPRxDATA`/`DATB` **a media línea** (patrón Brian the Lion: capas de 2
/// colores; lo emiten `SpriteLineLayer` y `SpriteHorizontalRearm`) necesita **hueco de DMA
/// dentro de la línea** para el fetch del sprite. Con **más de**
/// `kSpriteLineDataMaxBitplanes` planos el fetch de bitplanes ocupa la línea y el ciclo del
/// sprite no llega: en WinUAE la ventana de sprites se cierra en el arranque del secuenciador
/// de bitplanes (`custom.cpp:10111`, `hp <= plfstrt_sprite`; `custom.cpp:9888-9889` y
/// `10008-10009` la recortan a DDFSTRT/BPRUN) y el último ciclo de plano solo sirve a sprites
/// **si los planos acaban antes** (`custom.cpp:10130-10131`) — con 6 planos no acaban a
/// tiempo. Contraste: `sprite-tricks-games.md` §Brian the Lion («solo 4/5 planos»).
/// Códigos: 1 planos > `kSpriteLineDataMaxBitplanes`.
[[nodiscard]] constexpr CheckResult check_sprite_line_data(eng::u8 planes) noexcept {
	if (planes > eng::graphics::kSpriteLineDataMaxBitplanes) {
		return {1u, "parcheo SPRxDATA a media linea con demasiados planos", 0u,
			eng::graphics::kSpriteLineDataMaxBitplanes, planes};
	}
	return {};
}

// =====================================================================================
// Orden de los WAIT del Copper
// =====================================================================================

/// Comprueba que los WAIT de una copperlist **no retroceden**: sus posiciones `(vpos, hpos)`
/// son no decrecientes en el orden de la lista. El Copper ejecuta los WAIT en orden y un WAIT
/// "ya pasado" no espera a su línea: **se queda hasta el frame siguiente** (AHRM 3.ª, "Copper
/// WAIT"; lección de la demo 065: las bandas debían empezar tras el WAIT de la cabecera).
/// Detectarlo en setup convierte "la mitad del efecto no se ve" en un fallo con índice.
///
/// Decodificación: `inst` con bit 0 = 1 es WAIT (bit 0 = 0 es MOVE; `0xffff` es END y corta).
/// El par de overflow de `wait_line_pal` (`0xffdf`/`0xfffe`) **reinicia** la comparación (su
/// única función es forzar el wrap de VPOS para esperar líneas ≥ 256). Se comparan las palabras
/// **tal como se emiten** (es lo que ve el Copper), no la línea "lógica".
/// Código: 1 = WAIT retrocede.
[[nodiscard]] constexpr CheckResult check_copper_wait_order(const eng::u16* words,
							    eng::u16 words_count) noexcept {
	eng::u16 prev = 0u;
	bool have_prev = false;
	for (eng::u16 i = 0u; i + 1u < words_count; i = static_cast<eng::u16>(i + 2u)) {
		const eng::u16 inst = words[i];
		const eng::u16 data = words[i + 1u];
		if (inst == 0xffffu) {
			break; // END de la lista
		}
		if ((inst & 0x1u) == 0u) {
			continue; // MOVE
		}
		if (inst == 0xffdfu && data == 0xfffeu) {
			have_prev = false; // marcador de overflow de línea PAL: reinicia el orden
			continue;
		}
		const eng::u16 pos = static_cast<eng::u16>((inst & 0xff00u) | (inst & 0x00feu));
		if (have_prev && pos < prev) {
			return {1u, "WAIT del Copper retrocede (el efecto espera al frame siguiente)",
				i, prev, pos};
		}
		prev = pos;
		have_prev = true;
	}
	return {};
}

// =====================================================================================
// Combinaciones de BOB que el ejecutor no cubre
// =====================================================================================

/// Comprueba que la **combinación de campos** de un `Bob` está cubierta por el ejecutor de
/// dibujado. `bob_draw` documenta los casos que **no** cubre devolviendo `false` — un fallo
/// silencioso si la demo ignora el resultado (caso real: cookie-cut declarado `Interleaved`
/// con máscara en plano suelto → el caminante y el patrullero de la 066 no se dibujaban).
/// Códigos: 1 cookie-cut interleaved con máscara suelta (usar `InterleavedPair` o hoja
/// planar); 2 cookie-cut sin máscara.
[[nodiscard]] constexpr CheckResult check_bob_plan(const eng::graphics::Bob& bob) noexcept {
	using eng::graphics::BobDraw;
	using eng::graphics::BobLayout;
	using eng::graphics::BobMaskPack;
	if (bob.draw == BobDraw::CookieCut) {
		if (bob.layout == BobLayout::Interleaved &&
		    bob.mask_pack == BobMaskPack::SeparatePlane) {
			return {1u,
				"cookie-cut interleaved con mascara en plano suelto: el ejecutor no lo "
				"cubre (usar InterleavedPair o una hoja planar)",
				0u, 0u, 0u};
		}
		// `SeparatePlane` exige la máscara en su propia vista; con `InterleavedPair` va
		// DENTRO de la hoja (segunda mitad de cada fila de plano) y `bob.mask` vacío es lo
		// correcto ahí.
		if (bob.mask_pack == BobMaskPack::SeparatePlane && bob.mask.empty()) {
			return {2u, "cookie-cut sin mascara (SeparatePlane)", 0u, 0u, 0u};
		}
	}
	return {};
}

/// Como `check_bob_plan(bob)`, y además exige que el **formato de la hoja** case con el del
/// **target** (`BitmapView::layout`): el ejecutor deriva la geometría de lectura de la hoja
/// del layout del destino (stride 0 si es interleaved; bloques de plano si es planar), así
/// que una hoja con formato distinto al destino se **lee mal en silencio**. Cada bob puede
/// llevar su formato —es un campo por objeto— pero debe coincidir con el del bitmap al que
/// se dibuja. Código: 3 formato de hoja ≠ formato del target.
[[nodiscard]] constexpr CheckResult check_bob_plan(
	const eng::graphics::Bob& bob, const eng::graphics::BobTarget& target) noexcept {
	const CheckResult base = check_bob_plan(bob);
	if (!base.ok()) {
		return base;
	}
	if (bob.layout != target.layout) {
		return {3u,
			"formato de la hoja del BOB distinto al del target (se leeria mal en silencio)",
			0u, static_cast<eng::u32>(target.layout), static_cast<eng::u32>(bob.layout)};
	}
	return {};
}

} // namespace eng::setup
