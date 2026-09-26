#pragma once

/// \file bobs.hpp
/// **Capa de BOBs** (`eng::scene::BobLayer`): una hoja de sprites (`graphics::Sprite`) y `N`
/// **actores** (`BobActor`: posición, frame, visibilidad). El juego mueve actores por frame;
/// `emit` dibuja los visibles al `FramePlan` sin que vea `BlitJob`, minterns ni strides.
///
/// ```cpp
/// eng::scene::BobLayer bobs {};
/// bobs.set_sheet(nave);                     // Sprite del asset
/// bobs.resize(16);
/// for (u8 i = 0; i < 16; ++i) bobs[i] = { x, y, frame, true };
/// bobs.emit(plan, scene.bob_target());      // una pasada por actor visible
/// ```

#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_asset.hpp>

namespace eng::scene {

/// **Actor de una capa de BOBs**: dónde se dibuja y qué frame muestra. Es un valor ligero
/// (solo posición/frame) para una hoja homogénea; no confundir con `eng::scene::Actor`, el
/// objeto del sistema generacional (`actor_types.hpp`).
struct BobActor {
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u8 frame = 0u;
	bool visible = true;
};

/// Capa de BOBs de capacidad fija (sin heap): una hoja + actores.
class BobLayer {
public:
	static constexpr eng::u8 kMaxActors = 32u;

	/// Asocia la hoja de sprites (el `Sprite` del asset). Sin hoja válida, `emit` no dibuja.
	void set_sheet(eng::graphics::Sprite sheet) noexcept { m_sheet = sheet; }
	[[nodiscard]] const eng::graphics::Sprite& sheet() const noexcept { return m_sheet; }

	/// Fija el número de actores en uso (`<= kMaxActors`).
	void resize(eng::u8 n) noexcept { m_count = (n <= kMaxActors) ? n : kMaxActors; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }

	[[nodiscard]] BobActor& operator[](eng::u8 i) noexcept { return m_actors[i]; }
	[[nodiscard]] const BobActor& operator[](eng::u8 i) const noexcept { return m_actors[i]; }

	/// Dibuja los actores **visibles** en `target`; devuelve cuántos se dibujaron. `fine_scroll`
	/// (px) compensa el fine scroll del campo (`Band::bob_fine_scroll()`): el campo desplaza todo
	/// el playfield, así que el objeto se dibuja a `x - fine_scroll` para no "temblar".
	[[nodiscard]] eng::u16 emit(eng::graphics::FramePlan& plan,
				    const eng::graphics::BobTarget& target,
				    eng::u8 fine_scroll = 0u) const {
		eng::u16 drawn = 0u;
		for (eng::u8 i = 0u; i < m_count; ++i) {
			const BobActor& a = m_actors[i];
			const eng::s16 dx = a.x - fine_scroll; // compensa el fine scroll del campo
			if (a.visible && m_sheet.draw(plan, target, a.frame, dx, a.y)) {
				++drawn;
			}
		}
		return drawn;
	}

private:
	eng::graphics::Sprite m_sheet {};
	BobActor m_actors[kMaxActors] {};
	eng::u8 m_count = 0u;
};

/// Borra (`D = 0`) la caja `w x h` en `(x,y)` sobre **todos** los planos del destino, en un
/// solo blit intercalado. Es el complemento de `bob_erase_box` (mismo destino `BobTarget`)
/// para limpiar bandas/zonas del playfield sin describir un `BlitJob`: el juego escribe
/// `scene::clear_box(plan, scene.bob_target(), 0, 200, 320, 56)`.
[[nodiscard]] inline bool clear_box(eng::graphics::FramePlan& plan,
				    const eng::graphics::BobTarget& t, eng::s16 x, eng::s16 y,
				    eng::u16 w, eng::u16 h) {
	if (t.base == nullptr || t.planes == 0u || w == 0u || h == 0u) {
		return true;
	}
	const eng::s16 wx = static_cast<eng::s16>(x & ~15);
	if (wx < 0) {
		return true; // caja fuera por la izquierda
	}
	const bool inter = (t.layout == eng::graphics::BobLayout::Interleaved);
	// Con desplazamiento fino el blit procesa una palabra de mas (la guarda la absorbe en la
	// caja como en el dibujo): asi no queda residuo en el borde derecho.
	const eng::u16 words =
		static_cast<eng::u16>((w + 15u) / 16u + ((x & 15) != 0 ? 1u : 0u));
	const eng::u32 start_row =
		inter ? static_cast<eng::u32>(t.row_bytes) * t.planes : t.row_bytes;
	eng::graphics::BlitJob job {};
	job.destination = {reinterpret_cast<eng::u16*>(t.base + static_cast<eng::u32>(y) * start_row +
						       (static_cast<eng::u32>(wx) >> 3u))};
	job.words_per_row = words;
	job.height = inter ? static_cast<eng::u16>(h * t.planes) : h;
	job.destination_modulo_bytes =
		static_cast<eng::s16>(t.row_bytes - static_cast<eng::u32>(words) * 2u);
	job.bitplane_count = inter ? 1u : t.planes;
	job.destination_plane_stride_bytes = inter ? 0u : t.plane_bytes;
	job.interleaved = inter;
	job.minterm = 0x00u; // D = 0
	return plan.add_clear_rect(job);
}

namespace fast_bob_detail {

/// Rectángulo destino (posición + tamaño) para la política de Fast BOBs.
struct PRect {
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u16 w = 0;
	eng::u16 h = 0;
	/// `true` si el rectángulo no cubre área (sin pintar).
	[[nodiscard]] constexpr bool empty() const noexcept { return w == 0u || h == 0u; }
};

/// `true` si los dos rectángulos se solapan en área (ignora los vacíos).
[[nodiscard]] constexpr bool overlaps(const PRect& a, const PRect& b) noexcept {
	if (a.empty() || b.empty()) {
		return false;
	}
	return a.x < static_cast<eng::s16>(b.x + b.w) && b.x < static_cast<eng::s16>(a.x + a.w) &&
	       a.y < static_cast<eng::s16>(b.y + b.h) && b.y < static_cast<eng::s16>(a.y + a.h);
}

/// Menor rectángulo que cubre `a` y `b` (ignora los vacíos).
[[nodiscard]] constexpr PRect unite(const PRect& a, const PRect& b) noexcept {
	if (a.empty()) {
		return b;
	}
	if (b.empty()) {
		return a;
	}
	const eng::s16 minx = (a.x < b.x) ? a.x : b.x;
	const eng::s16 miny = (a.y < b.y) ? a.y : b.y;
	const eng::s16 maxx = (static_cast<eng::s16>(a.x + a.w) > static_cast<eng::s16>(b.x + b.w))
				      ? static_cast<eng::s16>(a.x + a.w)
				      : static_cast<eng::s16>(b.x + b.w);
	const eng::s16 maxy = (static_cast<eng::s16>(a.y + a.h) > static_cast<eng::s16>(b.y + b.h))
				      ? static_cast<eng::s16>(a.y + a.h)
				      : static_cast<eng::s16>(b.y + b.h);
	return PRect {minx, miny, static_cast<eng::u16>(maxx - minx),
		      static_cast<eng::u16>(maxy - miny)};
}

} // namespace fast_bob_detail

/// **Capa de BOBs rápidos (“Fast Bobs”)**: PF frontal vacío + BOBs con *padding* de color 0.
///
/// La técnica (Mega Typhoon / Roondar; ficha
/// `docs/reference/amiga/techniques/dual-playfield-fastbobs.md`): se dibuja cada BOB sobre un
/// playfield que arranca **vacío** (color 0), con una **copia** (minterm `$F0`, 2 canales DMA)
/// de su bitmap **con padding transparente** alrededor. El padding sobrescribe lo que quede del
/// frame anterior mientras el movimiento no supere el padding: dibuja y limpia en **un** blit,
/// sin save/restore ni cookie-cut por defecto.
///
/// El desarrollador **solo mueve actores** (`BobActor`) y llama `emit`; la capa decide, por
/// actor y por frame, entre el camino rápido (copia con padding) y el de **degradación**
/// (clear del rectángulo previo + cookie-cut `$CA`), que se activa cuando:
/// - el actor se movió más que el padding (la copia no cubriría los píxeles viejos), o
/// - su área (unión de lo pintado antes y lo que va a pintar) **se solapa** con la de otro
///   actor (la copia de uno borraría al otro: hace falta transparencia).
///
/// Requisitos que la capa asume y **no** puede verificar: el playfield de destino parte vacío;
/// `m_sheet` es el bitmap **con** padding (su tamaño es el rectángulo pintado) y `m_cookie` el
/// mismo gráfico **sin** padding pero con máscara. `pad_x`/`pad_y` deben ser ≥ el desplazamiento
/// máximo por frame (en píxeles).
class FastBobLayer {
public:
	static constexpr eng::u8 kMaxActors = 32u;

	/// Hoja **con padding** (copia opaca; su `Bob::draw` es `Opaque`) y el padding por lado.
	void set_sheet(eng::graphics::Sprite padded, eng::u8 pad_x, eng::u8 pad_y) noexcept {
		m_sheet = padded;
		m_pad_x = pad_x;
		m_pad_y = pad_y;
	}
	/// Hoja de degradación: mismo gráfico **con máscara** (`Bob::draw` = `CookieCut`).
	void set_slow_sheet(eng::graphics::Sprite cookie) noexcept { m_cookie = cookie; }

	void resize(eng::u8 n) noexcept { m_count = (n <= kMaxActors) ? n : kMaxActors; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }
	[[nodiscard]] eng::u8 pad_x() const noexcept { return m_pad_x; }
	[[nodiscard]] eng::u8 pad_y() const noexcept { return m_pad_y; }

	[[nodiscard]] BobActor& operator[](eng::u8 i) noexcept { return m_actors[i]; }
	[[nodiscard]] const BobActor& operator[](eng::u8 i) const noexcept { return m_actors[i]; }

	/// Dibuja y limpia según la política de Fast BOBs; devuelve cuántos actores se pintaron.
	/// `fine_scroll` (px) compensa el fine scroll del campo (`Band::bob_fine_scroll()`).
	[[nodiscard]] eng::u16 emit(eng::graphics::FramePlan& plan,
				    const eng::graphics::BobTarget& target,
				    eng::u8 fine_scroll = 0u) {
		using fast_bob_detail::PRect;
		using fast_bob_detail::overlaps;
		using fast_bob_detail::unite;
		const eng::s16 px = static_cast<eng::s16>(m_pad_x);
		const eng::s16 py = static_cast<eng::s16>(m_pad_y);
		const eng::u16 w = m_sheet.width();
		const eng::u16 h = m_sheet.height();

		PRect fast[kMaxActors] {};
		PRect span[kMaxActors] {};
		bool live[kMaxActors] {};
		bool slow[kMaxActors] {};
		for (eng::u8 i = 0u; i < m_count; ++i) {
			const BobActor& a = m_actors[i];
			live[i] = a.visible && w != 0u && h != 0u;
			if (!live[i]) {
				continue;
			}
			const eng::s16 fx = a.x - fine_scroll - px;
			const eng::s16 fy = a.y - py;
			fast[i] = PRect {fx, fy, w, h};
			// Área que el actor toca: lo que pintó antes y lo que pintará ahora.
			span[i] = unite(fast[i], m_painted[i]);
		}
		// Conflicto: dos áreas que se solapan -> ambos degradan (la copia sería destructiva).
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (!live[i]) continue;
			for (eng::u8 j = static_cast<eng::u8>(i + 1u); j < m_count; ++j) {
				if (live[j] && overlaps(span[i], span[j])) {
					slow[i] = true;
					slow[j] = true;
				}
			}
		}
		// Movimiento mayor que el padding: la copia no limpiaría los píxeles viejos.
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (!live[i]) continue;
			const BobActor& a = m_actors[i];
			const eng::s16 dx = static_cast<eng::s16>(a.x - m_prev_x[i]);
			const eng::s16 dy = static_cast<eng::s16>(a.y - m_prev_y[i]);
			const eng::s16 adx = (dx < 0) ? static_cast<eng::s16>(-dx) : dx;
			const eng::s16 ady = (dy < 0) ? static_cast<eng::s16>(-dy) : dy;
			if (m_has[i] && (adx > px || ady > py)) {
				slow[i] = true;
			}
		}

		// 1) Limpia las áreas previas de los que degradan (para que no queden restos).
		eng::u16 drawn = 0u;
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (live[i] && slow[i] && !m_painted[i].empty()) {
				(void)clear_box(plan, target, m_painted[i].x, m_painted[i].y,
						m_painted[i].w, m_painted[i].h);
			}
		}
		// 2) Dibuja: rápido (copia con padding) o degradado (cookie-cut sin padding).
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (!live[i]) {
				continue;
			}
			const BobActor& a = m_actors[i];
			bool ok = false;
			if (slow[i]) {
				const eng::s16 fx = a.x - fine_scroll;
				ok = m_cookie.draw(plan, target, a.frame, fx, a.y);
				m_painted[i] = PRect {fx, a.y, m_cookie.width(), m_cookie.height()};
			} else {
				ok = m_sheet.draw(plan, target, a.frame, fast[i].x, fast[i].y);
				m_painted[i] = fast[i];
			}
			m_prev_x[i] = a.x;
			m_prev_y[i] = a.y;
			m_has[i] = true;
			if (ok) {
				++drawn;
			}
		}
		return drawn;
	}

	/// Olvida el historial (tras un clear total del playfield de BOBs).
	void reset_history() noexcept {
		for (eng::u8 i = 0u; i < kMaxActors; ++i) {
			m_painted[i] = {};
			m_has[i] = false;
		}
	}

private:
	eng::graphics::Sprite m_sheet {};
	eng::graphics::Sprite m_cookie {};
	BobActor m_actors[kMaxActors] {};
	fast_bob_detail::PRect m_painted[kMaxActors] {};
	eng::s16 m_prev_x[kMaxActors] {};
	eng::s16 m_prev_y[kMaxActors] {};
	bool m_has[kMaxActors] {};
	eng::u8 m_count = 0u;
	eng::u8 m_pad_x = 0u;
	eng::u8 m_pad_y = 0u;
};

} // namespace eng::scene
