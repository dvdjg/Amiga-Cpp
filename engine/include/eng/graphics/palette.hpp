#pragma once

/// \file palette.hpp
/// **Paleta de juego**: valor de 32 colores RGB444 con las operaciones de *intención*
/// (`set`/`get`/`fill`/`fade`/`mix`) y una vía de publicación a un `FramePlan`.
///
/// Es la capa de ergonomía que faltaba sobre `Palette32` (almacenamiento) y
/// `eng::util` (aritmética de color): el juego escribe `pal.set(ColorIndex{1},
/// Color::rgb(15, 8, 0))` o `pal.fade(1, 2)` en vez de manipular a mano el array
/// `color[]` y llamar a `palette_scale`/`palette_lerp`. `Palette` **no** conoce
/// registros de hardware ni `color_register`: la materialización la decide el driver
/// (parche base del `FramePlan`, zona de Copper, doble buffer...).
///
/// ```text
///   Color (RGB444) · ColorIndex (0..31)     ← tipos de dominio
///   Palette (valor, 32 colores)             ← set/get/fill/fade/mix
///        │ words() / operator PaletteWords
///        ├─► Palette32 (almacenamiento)     ← base de escena EHB, fundido, ciclo
///        └─► FramePlan::add_base_palette_patch   (apply)
///
///   herramientas C++23: `constexpr` (fade/mix se evalúan en compilación) · tipos fuertes
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/color.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/palette32.hpp>

namespace eng {

/// Índice de un registro de color físico (0..31).
struct ColorIndex {
	u8 value = 0;
	[[nodiscard]] constexpr bool valid() const noexcept { return value < 32u; }
};

/// Construye un `ColorIndex` válido: recorta a `31` si `i` se sale (no hay índice inválido).
[[nodiscard]] constexpr ColorIndex color_index(u8 i) noexcept {
	return ColorIndex {static_cast<u8>(i < 32u ? i : 31u)};
}

/// Color RGB444 (`0x0RGB`) con constructores con nombre que validan el rango 0..15.
struct Color {
	u16 value = 0; ///< empaquetado `0x0RGB`

	[[nodiscard]] static constexpr Color rgb(u16 r, u16 g, u16 b) noexcept {
		return Color {eng::util::rgb444(r, g, b)};
	}
	/// Envuelve un `0x0RGB` ya empaquetado (se enmascara a 12 bits).
	[[nodiscard]] static constexpr Color from_raw(u16 rgb444) noexcept {
		return Color {static_cast<u16>(rgb444 & 0x0fffu)};
	}
	[[nodiscard]] constexpr u16 r() const noexcept { return eng::util::rgb444_r(value); }
	[[nodiscard]] constexpr u16 g() const noexcept { return eng::util::rgb444_g(value); }
	[[nodiscard]] constexpr u16 b() const noexcept { return eng::util::rgb444_b(value); }
	[[nodiscard]] constexpr bool operator==(Color o) const noexcept { return value == o.value; }
};

/// Paleta de 32 colores con operaciones de intención. Tipo de **valor**: se copia y se
/// pasa por referencia, no posee recursos ni apunta a hardware.
class Palette {
public:
	constexpr Palette() = default;
	/// Copia el contenido de una `Palette32` de escena (para tomar una instantánea).
	explicit constexpr Palette(const Palette32& src) noexcept : m_data(src) {}
	explicit constexpr Palette(PaletteWords src) noexcept { copy_from(src); }

	[[nodiscard]] static constexpr usize count() noexcept { return 32u; }
	[[nodiscard]] constexpr bool valid_index(ColorIndex i) const noexcept { return i.valid(); }

	/// Fija un color. El índice se recorta a 0..31 (no hay fallo que comprobar).
	constexpr void set(ColorIndex i, Color c) noexcept {
		m_data.color[color_index(i.value).value] = static_cast<u16>(c.value & 0x0fffu);
	}
	[[nodiscard]] constexpr Color get(ColorIndex i) const noexcept {
		return Color {static_cast<u16>(m_data.color[color_index(i.value).value] & 0x0fffu)};
	}

	constexpr void fill(Color c) noexcept {
		for (u8 i = 0; i < 32u; ++i) {
			m_data.color[i] = static_cast<u16>(c.value & 0x0fffu);
		}
	}
	constexpr void copy_from(const Palette32& src) noexcept { m_data = src; }
	constexpr void copy_from(PaletteWords src) noexcept {
		for (u8 i = 0; i < 32u; ++i) {
			m_data.color[i] = i < src.size() ? static_cast<u16>(src.data()[i] & 0x0fffu) : 0u;
		}
	}

	/// **Fundido *in place***: cada canal a `num/den` (`0` apaga la paleta, `num == den` la
	/// deja igual). Es `util::palette_scale` sobre los 32 colores.
	constexpr void fade(u16 num, u16 den) noexcept {
		(void)eng::util::palette_scale({m_data.color, 32u}, {m_data.color, 32u}, num, den);
	}
	/// Fundido desde `src` **sin** tocar `src` (`m_data[i] = scale(src[i], num, den)`).
	constexpr void fade_from(const Palette32& src, u16 num, u16 den) noexcept {
		(void)eng::util::palette_scale({m_data.color, 32u}, {src.color, 32u}, num, den);
	}
	/// **Interpola** `a`→`b` (`num == 0` deja `a`, `num == den` deja `b`) en el tramo
	/// `[first, first+count)`; el resto de colores queda igual a `a`. Es `util::palette_lerp`.
	constexpr void mix(const Palette32& a, const Palette32& b, u16 num, u16 den, u8 first = 0u,
			   u8 count = 32u) noexcept {
		m_data = a;
		usize n = 0u;
		if (first < 32u) {
			n = count;
			if (static_cast<usize>(first) + n > 32u) {
				n = 32u - first;
			}
		}
		(void)eng::util::palette_lerp({m_data.color + first, n}, {a.color + first, n},
					      {b.color + first, n}, num, den);
	}
	[[nodiscard]] constexpr const Palette32& storage() const noexcept { return m_data; }
	[[nodiscard]] constexpr Palette32& storage() noexcept { return m_data; }
	/// Vista de dominio para las APIs que piden `PaletteWords` (p. ej. `emit_palette`).
	[[nodiscard]] constexpr PaletteWords words() const noexcept { return m_data.words(); }
	constexpr operator PaletteWords() const noexcept { return m_data.words(); }

	/// Registra el parche base de paleta en el plan; el driver lo materializa. `false` si no
	/// cabe (límite de parches del plan).
	[[nodiscard]] bool apply(graphics::FramePlan& plan, u8 first = 0u, u8 count = 32u) const {
		return plan.add_base_palette_patch(words(), first, count);
	}

private:
	Palette32 m_data {};
};

} // namespace eng
