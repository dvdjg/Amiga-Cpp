#pragma once

/// \file palette_generator.hpp
/// Generación determinista de paletas RGB444 para cuantización y tramado offline.
/// La selección es una heurística de muestreo reproducible, no una solución óptima
/// universal: la calidad depende de la imagen, la métrica perceptual y la matriz de dither.
/// La función es `constexpr`, no usa coma flotante y solo cuesta durante compilación/setup.
/// Verificación: HOST-483 (contrato constexpr) y demo Copper 086 (render A500 OCS/EHB; `demos/techniques/amiga/copper/086_palette_checkerboard/VALIDATION.md`).
///
/// \code
/// constexpr auto palette = eng::graphics::generate_palette<5>({
///     .transparent_index_zero = true,
///     .include_black = true,
///     .include_white = true,
/// });
/// constexpr auto ehb = eng::graphics::generate_palette<5, eng::graphics::PaletteMode::Ehb>();
/// \endcode

#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>

namespace eng::graphics {

/// Modo de salida: Normal entrega los registros disponibles; Ehb devuelve 32 bases y sus
/// 32 colores half-bright derivados mediante desplazamiento de cada componente RGB444.
enum class PaletteMode : u8 { Normal, Ehb };

/// Opciones para el generador. Los pesos son enteros para mantener constexpr portable.
struct PaletteGeneratorParams {
	bool transparent_index_zero = true; ///< Reserva el índice 0 como negro/transparente por convención.
	bool include_black = true;           ///< Incluye negro entre los colores útiles cuando hay espacio.
	bool include_white = true;           ///< Incluye blanco entre los colores útiles cuando hay espacio.
	u8 extra_grays = 3u;                 ///< Máximo de grises interiores reservados antes del muestreo general.
	u8 green_weight = 6u;                ///< Peso de G al ordenar por luminancia; no cambia el conjunto muestreado.
};

namespace detail {

/// Empaqueta componentes RGB444, limitando cada canal al dominio 0..15.
[[nodiscard]] constexpr u16 palette_rgb(u16 r, u16 g, u16 b) noexcept {
	return static_cast<u16>(((r & 15u) << 8u) | ((g & 15u) << 4u) | (b & 15u));
}

/// Comprueba si el color ya aparece en el prefijo ocupado de la tabla.
template <usize Capacity>
[[nodiscard]] constexpr bool palette_contains(const eng::util::Array<u16, Capacity>& colors,
						      usize count, u16 color) noexcept {
	for (usize i = 0u; i < count; ++i) {
		if (colors[i] == color) { return true; }
	}
	return false;
}

/// Inserta un color solo si es nuevo y queda espacio; devuelve si lo añadió.
template <usize Capacity>
constexpr bool palette_append(eng::util::Array<u16, Capacity>& colors, usize& count,
			      u16 color) noexcept {
	if (count >= Capacity || palette_contains(colors, count, color)) { return false; }
	colors[count++] = color;
	return true;
}

/// Luminancia ponderada usada únicamente para ordenar los colores sin alterar sus índices
/// de uso; los empates se resuelven por valor RGB444 para hacer la salida reproducible.
[[nodiscard]] constexpr u16 palette_luminance(u16 color, u8 green_weight) noexcept {
	const u16 r = static_cast<u16>((color >> 8u) & 15u);
	const u16 g = static_cast<u16>((color >> 4u) & 15u);
	const u16 b = static_cast<u16>(color & 15u);
	const u16 gw = green_weight == 0u ? 1u : green_weight;
	return static_cast<u16>(3u * r + gw * g + b);
}

} // namespace detail

/// Genera una tabla RGB444 fija para 3, 4 o 5 planos. El índice 0 reservado se escribe como
/// negro; en modo EHB los 32 índices superiores son exactamente las bases con cada nibble >> 1.
/// El orden de las bases es luminancia creciente, con desempate RGB444. No garantiza el máximo
/// número de promedios checkerboard: para ello habría que optimizar conjuntamente paleta, imagen,
/// métrica y patrón de dither.
template <u8 Planes, PaletteMode Mode = PaletteMode::Normal>
[[nodiscard]] constexpr auto generate_palette(PaletteGeneratorParams params = {}) noexcept {
	static_assert(Planes >= 3u && Planes <= 5u, "El modo normal admite de 3 a 5 planos");
	static_assert(Mode == PaletteMode::Normal || Planes == 5u,
		      "EHB requiere 32 colores base (5 planos más el bit half-bright)");
	constexpr usize kEntries = Mode == PaletteMode::Ehb ? 64u : (usize{1u} << Planes);
	constexpr usize kBaseEntries = Mode == PaletteMode::Ehb ? 32u : kEntries;
	eng::util::Array<u16, kEntries> result {};
	eng::util::Array<u16, 32u> candidates {};
	usize count = 0u;
	const bool reserve_zero = params.transparent_index_zero;
	const usize first_useful = reserve_zero ? 1u : 0u;
	const usize useful_capacity = kBaseEntries - first_useful;
	usize useful = 0u;

	// Index 0 siempre es negro de hardware; transparencia es una convención del consumidor.
	if (reserve_zero) { result[0u] = detail::palette_rgb(0u, 0u, 0u); }
	if (params.include_black && useful < useful_capacity) {
		(void)detail::palette_append(candidates, count, detail::palette_rgb(0u, 0u, 0u));
		++useful;
	}
	if (params.include_white && useful < useful_capacity) {
		(void)detail::palette_append(candidates, count, detail::palette_rgb(15u, 15u, 15u));
		++useful;
	}

	// Reserva rampas de gris interiores, valiosas para ilustración y gradientes, sin duplicar extremos.
	for (u16 step = 1u; step < 15u && useful < useful_capacity &&
				     step <= static_cast<u16>(params.extra_grays); ++step) {
		if (detail::palette_append(candidates, count, detail::palette_rgb(step, step, step))) { ++useful; }
	}

	// Un recorrido afín módulo 4096 visita cada RGB444 una vez y evita estado/float en constexpr.
	// La multiplicación impar y el incremento impar forman una permutación de los 12 bits.
	for (u16 n = 0u; useful < useful_capacity && n < 4096u; ++n) {
		const u16 raw = static_cast<u16>((n * 4051u + 173u) & 0x0fffu);
		const u16 r = static_cast<u16>((raw >> 8u) & 15u);
		const u16 g = static_cast<u16>((raw >> 4u) & 15u);
		const u16 b = static_cast<u16>(raw & 15u);
		if (detail::palette_append(candidates, count, detail::palette_rgb(r, g, b))) { ++useful; }
	}

	// Inserción ordenada de 32 elementos: coste acotado y ejecutado solo en compilación/setup.
	for (usize i = 1u; i < count; ++i) {
		const u16 value = candidates[i];
		usize j = i;
		while (j > 0u) {
			const u16 previous = candidates[j - 1u];
			const u16 lv = detail::palette_luminance(value, params.green_weight);
			const u16 lp = detail::palette_luminance(previous, params.green_weight);
			if (lp < lv || (lp == lv && previous <= value)) { break; }
			candidates[j] = previous;
			--j;
		}
		candidates[j] = value;
	}

	for (usize i = 0u; i < count; ++i) { result[first_useful + i] = candidates[i]; }
	if constexpr (Mode == PaletteMode::Ehb) {
		for (usize i = 0u; i < 32u; ++i) {
			const u16 base = result[i];
			result[32u + i] = detail::palette_rgb(static_cast<u16>(((base >> 8u) & 15u) >> 1u),
							      static_cast<u16>(((base >> 4u) & 15u) >> 1u),
							      static_cast<u16>((base & 15u) >> 1u));
		}
	}
	return result;
}

} // namespace eng::graphics
