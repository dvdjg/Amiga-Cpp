#pragma once

/// \file sprite_asset.hpp
/// **Sprite de juego**: un BOB con nombre de dominio. Reúne en un solo valor la geometría
/// (ancho/alto/planos/frames), la hoja de frames (y su máscara opcional) y la política de
/// dibujo/borrado, de modo que el juego llama `sprite.draw(plan, target, frame, x, y)` sin
/// conocer módulos, minterns ni strides.
///
/// Es la capa de juego sobre `graphics::bob`: el contrato de la hoja, el cookie-cut y el
/// desplazamiento fino siguen viviendo en `bob.hpp` (una sola verdad); este tipo solo los
/// expone con un vocabulario de objeto. Como la geometría viaja **con** el asset, no se
/// puede dibujar con un número de planos o un layout que no le correspondan.
///
/// No confundir con `graphics/sprite.hpp`, que describe **sprites hardware**
/// (`HwSpriteTemplate`/`HwSpritePlacement`); éste es el objeto de bitmap (BOB) que dibuja el
/// Blitter. Ver `docs/engine/architecture/PUBLIC_GAME_API.md` §2.1.1.
///
/// ```cpp
/// eng::graphics::Bob b {};
/// b.sheet = sheet; b.width = 32; b.height = 32; b.planes = 3; b.draw = BobDraw::Or;
/// const eng::graphics::Sprite nave {b};   // o el `Bob` ya cocinado del asset
/// // por frame:
/// nave.draw(plan, target, frame, x, y);   // añade el/los BlitJob(s)
/// ```
///
/// El `BobTarget` (destino) lo prepara el **contexto de dispositivo** (p. ej.
/// `Scene::bob_target()`), no el juego. Ver
/// `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>

namespace eng::graphics {

/// Asset de sprite de juego (BOB): geometría + hoja + máscara + política, sin poseer memoria.
class Sprite {
public:
	constexpr Sprite() = default;
	/// Envuelve un `Bob` ya descrito. `sheet_bytes`/`mask_bytes` son el tamaño de la hoja y la
	/// máscara (0 = desconocido); se necesitan para exponer la **vista de dominio** `visual()`.
	explicit constexpr Sprite(const Bob& b, u32 sheet_bytes = 0u, u32 mask_bytes = 0u) noexcept
		: m_bob(b), m_sheet_bytes(sheet_bytes), m_mask_bytes(mask_bytes) {}

	[[nodiscard]] constexpr u16 width() const noexcept { return m_bob.width; }
	[[nodiscard]] constexpr u16 height() const noexcept { return m_bob.height; }
	[[nodiscard]] constexpr u8 planes() const noexcept { return m_bob.planes; }
	[[nodiscard]] constexpr u8 frames() const noexcept { return m_bob.frame_count; }
	[[nodiscard]] constexpr BobLayout layout() const noexcept { return m_bob.layout; }
	[[nodiscard]] constexpr BobDraw draw_mode() const noexcept { return m_bob.draw; }
	[[nodiscard]] constexpr u32 sheet_bytes() const noexcept { return m_sheet_bytes; }
	[[nodiscard]] constexpr u32 mask_bytes() const noexcept { return m_mask_bytes; }
	/// `true` si tiene hoja y geometría mínima (ancho/alto/planos != 0).
	[[nodiscard]] constexpr bool valid() const noexcept {
		return m_bob.sheet != nullptr && m_bob.width != 0u && m_bob.height != 0u &&
		       m_bob.planes != 0u;
	}

	/// El `Bob` subyacente (para APIs que aún piden el tipo de bajo nivel).
	[[nodiscard]] constexpr const Bob& bob() const noexcept { return m_bob; }
	[[nodiscard]] constexpr Bob& bob() noexcept { return m_bob; }

	/// **Vista de dominio** (`Visual`) equivalente, para el camino de actores/planner (un único
	/// descriptor de objeto). El campo `pixels` queda vacío si no se declaró `sheet_bytes`.
	[[nodiscard]] Visual visual() const noexcept {
		Visual v {};
		v.kind = VisualKind::Bob;
		v.pixels = eng::Span<const u16> {reinterpret_cast<const u16*>(m_bob.sheet),
						 m_sheet_bytes / 2u};
		v.mask = (m_bob.mask != nullptr)
				 ? eng::Span<const u16> {reinterpret_cast<const u16*>(m_bob.mask),
							 m_mask_bytes / 2u}
				 : eng::Span<const u16> {};
		v.w = m_bob.width;
		v.h = m_bob.height;
		v.bitplanes = m_bob.planes;
		v.frame_count = m_bob.frame_count;
		v.frame_stride = m_bob.frame_stride;
		return v;
	}

	/// Dibuja el frame `frame` en `(x, y)`. Añade el/los `BlitJob(s)` al plan.
	/// `false` si el sprite es inválido, el frame está fuera de rango, o el cookie-cut
	/// interleaved no lo cubre el ejecutor (ver `bob.hpp`).
	[[nodiscard]] bool draw(FramePlan& plan, const BobTarget& target, u8 frame, s16 x,
				s16 y) const {
		return bob_draw(plan, m_bob, frame, x, y, target);
	}

	/// Borra la caja del sprite en `(x, y)` si su política es `BobErase::ClearRect`
	/// (con otro algoritmo no hace nada).
	[[nodiscard]] bool erase(FramePlan& plan, const BobTarget& target, s16 x, s16 y) const {
		return bob_erase(plan, m_bob, x, y, target);
	}

private:
	Bob m_bob {};
	u32 m_sheet_bytes = 0u;
	u32 m_mask_bytes = 0u;
};

} // namespace eng::graphics
