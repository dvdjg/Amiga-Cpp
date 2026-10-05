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

#include <eng/graphics/anim.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/scene/band_plan.hpp>

namespace eng::scene {

/// **Actor de una capa de BOBs**: dónde se dibuja, qué frame muestra y su orden. Es un valor
/// ligero para una hoja homogénea; no confundir con `eng::scene::Actor`, el objeto del sistema
/// generacional (`actor_types.hpp`). Si `anim` es válida, su frame manda sobre `frame` (el actor
/// queda **ligado** a su animación sin que el juego lleve el índice a mano).
struct BobActor {
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u8 frame = 0u;
	bool visible = true;
	/// Hoja de la capa (`BobLayer::kMaxSheets`) que usa este actor: permite una capa
	/// **heterogénea** (varias hojas) sin duplicar el tipo. 0 = hoja principal.
	eng::u8 sheet_index = 0u;
	/// Orden de superposición **dentro de la capa** (mayor = delante). `emit` ordena por `z`.
	eng::u8 z = 128u;
	/// Animación opcional (vistas no propietarias): si `valid()`, `emit` usa su frame y `tick()`
	/// la avanza. El juego solo rellena `frames`/`durations` desde el asset.
	eng::graphics::Anim anim {};
	[[nodiscard]] bool animated() const noexcept { return anim.valid(); }
};

/// Capa de BOBs de capacidad fija (sin heap): una hoja + actores.
class BobLayer {
public:
	static constexpr eng::u8 kMaxActors = 32u;
	static constexpr eng::u8 kMaxSheets = 4u; ///< hojas simultáneas (capa heterogénea)

	/// Asocia la hoja **principal** (`index 0`). Sin hoja válida, `emit` no dibuja ese actor.
	void set_sheet(eng::graphics::Sprite sheet) noexcept { m_sheets[0] = sheet; }
	/// Asocia una hoja **adicional** (capa heterogénea); el actor la elige con `sheet_index`.
	void set_sheet(eng::u8 index, eng::graphics::Sprite sheet) noexcept {
		if (index < kMaxSheets) m_sheets[index] = sheet;
	}
	[[nodiscard]] const eng::graphics::Sprite& sheet(eng::u8 index = 0u) const noexcept {
		return m_sheets[index < kMaxSheets ? index : 0u];
	}

	/// Fija el número de actores en uso (`<= kMaxActors`).
	void resize(eng::u8 n) noexcept { m_count = (n <= kMaxActors) ? n : kMaxActors; }
	[[nodiscard]] eng::u8 count() const noexcept { return m_count; }

	[[nodiscard]] BobActor& operator[](eng::u8 i) noexcept { return m_actors[i]; }
	[[nodiscard]] const BobActor& operator[](eng::u8 i) const noexcept { return m_actors[i]; }

	/// Avanza **un frame de juego** la animación de todos los actores que la tengan. El juego lo
	/// llama en su `update`; `emit` no avanza nada (solo dibuja).
	void tick() noexcept {
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (m_actors[i].anim.valid()) m_actors[i].anim.update();
		}
	}

	/// Dibuja los actores **visibles** en `target`, ordenados por `z` (menor primero; el mayor
	/// queda delante); devuelve cuántos se dibujaron. El frame es el de la animación si es válida.
	/// `fine_scroll` (px) compensa el fine scroll del campo (`Band::bob_fine_scroll()`): el campo
	/// desplaza todo el playfield, así que el objeto se dibuja a `x - fine_scroll` para no "temblar".
	[[nodiscard]] eng::u16 emit(eng::graphics::FramePlan& plan,
				    const eng::graphics::BobTarget& target,
				    eng::u8 fine_scroll = 0u) const {
		// Orden por `z` estable (inserción; `kMaxActors` es pequeño): sin heap ni allocaciones.
		eng::u8 order[kMaxActors];
		eng::u8 n = 0u;
		for (eng::u8 i = 0u; i < m_count; ++i) {
			eng::u8 at = n;
			while (at > 0u && m_actors[order[at - 1u]].z > m_actors[i].z) {
				order[at] = order[at - 1u];
				--at;
			}
			order[at] = i;
			++n;
		}
		eng::u16 drawn = 0u;
		for (eng::u8 j = 0u; j < n; ++j) {
			const BobActor& a = m_actors[order[j]];
			const eng::s16 dx = a.x - fine_scroll; // compensa el fine scroll del campo
			const eng::u8 frame = a.anim.valid() ? a.anim.frame() : a.frame;
			const eng::graphics::Sprite& sh = sheet(a.sheet_index);
			if (a.visible && sh.draw(plan, target, frame, dx, a.y)) {
				++drawn;
			}
		}
		return drawn;
	}

	/// **Emite cada actor a la banda que contiene su `y`** (`ROADMAP_GAME_API.md` §7, split-screen):
	/// `targets[i]`/`fine[i]` son el `BobTarget` y el fine scroll de la banda `bands[i]`. El actor va
	/// en **coordenadas de pantalla**; la capa resta el `top` de su banda (el target no lo compensa).
	/// Un actor cuya `y` cae fuera de toda banda (hueco) no se dibuja. Sin allocaciones.
	[[nodiscard]] eng::u16 emit_banded(eng::graphics::FramePlan& plan,
					   const eng::Span<const BandSpan> bands,
					   const eng::Span<const eng::graphics::BobTarget> targets,
					   const eng::Span<const eng::u8> fine = {}) const {
		eng::u8 order[kMaxActors];
		eng::u8 n = 0u;
		for (eng::u8 i = 0u; i < m_count; ++i) {
			eng::u8 at = n;
			while (at > 0u && m_actors[order[at - 1u]].z > m_actors[i].z) {
				order[at] = order[at - 1u];
				--at;
			}
			order[at] = i;
			++n;
		}
		eng::u16 drawn = 0u;
		for (eng::u8 j = 0u; j < n; ++j) {
			const BobActor& a = m_actors[order[j]];
			if (!a.visible) {
				continue;
			}
			const eng::u16 bi = band_containing(bands, static_cast<eng::u16>(a.y));
			if (bi >= targets.size()) {
				continue;
			}
			const eng::u8 fs = bi < fine.size() ? fine[bi] : 0u;
			const eng::u8 frame = a.anim.valid() ? a.anim.frame() : a.frame;
			const eng::graphics::Sprite& sh = sheet(a.sheet_index);
			// Y relativa a la banda (el `bob_target` no compensa el `top`).
			const eng::s16 ty = static_cast<eng::s16>(a.y - static_cast<eng::s16>(bands[bi].top));
			if (sh.draw(plan, targets[bi], frame, static_cast<eng::s16>(a.x - fs), ty)) {
				++drawn;
			}
		}
		return drawn;
	}

private:
	eng::graphics::Sprite m_sheets[kMaxSheets] {};
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
	if (t.planes.empty() || t.plane_count == 0u || w == 0u || h == 0u) {
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
	const eng::u32 start_row = inter ? static_cast<eng::u32>(t.row_bytes) * t.plane_count : t.row_bytes;
	const bool full_interleaved_rows = inter && wx == 0 &&
		static_cast<eng::u32>(words) * sizeof(eng::u16) == t.row_bytes;
	// **Construcción IN SITU** (coste cero): la ranura del plan, sin `BlitJob` local ni copia.
	eng::graphics::BlitJob& job = plan.begin_blit_job(eng::graphics::BlitJobKind::ClearRect);
	job.destination = eng::graphics::BlitPtr::from_storage(reinterpret_cast<eng::u16*>(
		t.data() + static_cast<eng::u32>(y) * start_row + (static_cast<eng::u32>(wx) >> 3u)));
	job.words_per_row = words;
	// La ranura es estable (se reutiliza cada frame): fijar todo campo que el encoder lea.
	job.source_words_per_row = 0u;
	job.source_modulo_bytes = 0;
	job.descending = false;
	job.source_shift = 0u;
	if (full_interleaved_rows) {
		// Con toda la fila física cubierta, los planos intercalados son un bloque continuo:
		// un único blit D-only recorre las filas de todos los planos.
		job.height = static_cast<eng::u16>(h * t.plane_count);
		job.destination_modulo_bytes = 0;
		job.bitplane_count = 1u;
		job.destination_plane_stride_bytes = 0u;
		job.interleaved = true;
		job.minterm = 0x00u; // D = 0
		return plan.commit_blit_job();
	}
	if (inter) {
		// Una caja parcial requiere saltar las filas físicas de los otros planos.
		job.height = h;
		job.destination_modulo_bytes = static_cast<eng::s16>(
			static_cast<eng::u32>(t.row_bytes) * t.plane_count -
			static_cast<eng::u32>(words) * sizeof(eng::u16));
		job.bitplane_count = t.plane_count;
		job.destination_plane_stride_bytes = t.plane_pointer_step();
		job.interleaved = true;
		job.minterm = 0x00u; // D = 0
		return plan.commit_blit_job();
	}
	job.height = h;
	const eng::u32 bitmap_row_bytes = t.row_bytes;
	job.destination_modulo_bytes = static_cast<eng::s16>(
		bitmap_row_bytes - static_cast<eng::u32>(words) * sizeof(eng::u16));
	job.bitplane_count = t.plane_count;
	job.destination_plane_stride_bytes = t.plane_pointer_step();
	job.interleaved = false;
	job.minterm = 0x00u; // D = 0
	return plan.commit_blit_job();
}

namespace fast_bob_detail {

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

	/// Hoja **con padding** (copia opaca; su `Bob::draw` es `Opaque`) y el **padding por lado**, que
	/// debe ser **≥ el desplazamiento máximo del actor por frame (px)** — es lo que garantiza que la
	/// copia rápida borre el rectángulo previo en el mismo blit. Se consulta con `pad_x()`/`pad_y()`.
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
		const eng::s16 px = static_cast<eng::s16>(m_pad_x);
		const eng::s16 py = static_cast<eng::s16>(m_pad_y);
		const eng::u16 w = m_sheet.width();
		const eng::u16 h = m_sheet.height();

		eng::Box fast[kMaxActors] {};
		eng::Box span[kMaxActors] {};
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
			fast[i] = eng::Box {fx, fy, w, h};
			// Área que el actor toca: lo que pintó antes y lo que pintará ahora.
			span[i] = eng::merge(fast[i], m_painted[i]);
		}
		// Conflicto: dos áreas que se solapan -> ambos degradan (la copia sería destructiva).
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (!live[i]) continue;
			for (eng::u8 j = static_cast<eng::u8>(i + 1u); j < m_count; ++j) {
				if (live[j] && eng::overlaps(span[i], span[j])) {
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
				m_painted[i] = eng::Box {fx, a.y, m_cookie.width(), m_cookie.height()};
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

	/// **Emite cada actor a la banda que contiene su `y`** (split-screen), con la misma política de
	/// Fast BOBs que `emit` pero `targets[i]`/`fine[i]` por banda. El **conflicto** (degradación) se
	/// evalúa **dentro de cada banda** (actores en bitmaps distintos no se pisan). Actores en
	/// coordenadas de pantalla; la capa resta el `top` de su banda. Sin allocaciones.
	[[nodiscard]] eng::u16 emit_banded(eng::graphics::FramePlan& plan,
					   const eng::Span<const BandSpan> bands,
					   const eng::Span<const eng::graphics::BobTarget> targets,
					   const eng::Span<const eng::u8> fine = {}) {
		const eng::s16 px = static_cast<eng::s16>(m_pad_x);
		const eng::s16 py = static_cast<eng::s16>(m_pad_y);
		const eng::u16 w = m_sheet.width();
		const eng::u16 h = m_sheet.height();
		eng::Box fast[kMaxActors] {};
		eng::Box span[kMaxActors] {};
		eng::u16 band[kMaxActors] {};
		bool live[kMaxActors] {};
		bool slow[kMaxActors] {};
		for (eng::u8 i = 0u; i < m_count; ++i) {
			const BobActor& a = m_actors[i];
			const eng::u16 bi = band_containing(bands, static_cast<eng::u16>(a.y));
			const eng::u8 fs = bi < fine.size() ? fine[bi] : 0u;
			live[i] = a.visible && w != 0u && h != 0u && bi < targets.size();
			if (!live[i]) {
				continue;
			}
			band[i] = bi;
			const eng::s16 by = static_cast<eng::s16>(a.y - static_cast<eng::s16>(bands[bi].top));
			fast[i] = eng::Box {static_cast<eng::s16>(a.x - fs - px),
					    static_cast<eng::s16>(by - py), w, h};
			span[i] = eng::merge(fast[i], m_painted[i]);
		}
		// Conflicto solo entre actores de la MISMA banda.
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (!live[i]) continue;
			for (eng::u8 j = static_cast<eng::u8>(i + 1u); j < m_count; ++j) {
				if (live[j] && band[i] == band[j] && eng::overlaps(span[i], span[j])) {
					slow[i] = true;
					slow[j] = true;
				}
			}
		}
		// Movimiento mayor que el padding.
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
		eng::u16 drawn = 0u;
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (live[i] && slow[i] && !m_painted[i].empty()) {
				(void)clear_box(plan, targets[band[i]], m_painted[i].x, m_painted[i].y,
						m_painted[i].w, m_painted[i].h);
			}
		}
		for (eng::u8 i = 0u; i < m_count; ++i) {
			if (!live[i]) {
				continue;
			}
			const BobActor& a = m_actors[i];
			const eng::u16 bi = band[i];
			const eng::u8 fs = bi < fine.size() ? fine[bi] : 0u;
			const eng::s16 by = static_cast<eng::s16>(a.y - static_cast<eng::s16>(bands[bi].top));
			bool ok = false;
			if (slow[i]) {
				ok = m_cookie.draw(plan, targets[bi], a.frame,
						   static_cast<eng::s16>(a.x - fs), by);
				m_painted[i] = eng::Box {static_cast<eng::s16>(a.x - fs), by,
							 m_cookie.width(), m_cookie.height()};
			} else {
				ok = m_sheet.draw(plan, targets[bi], a.frame, fast[i].x, fast[i].y);
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
	eng::Box m_painted[kMaxActors] {};
	eng::s16 m_prev_x[kMaxActors] {};
	eng::s16 m_prev_y[kMaxActors] {};
	bool m_has[kMaxActors] {};
	eng::u8 m_count = 0u;
	eng::u8 m_pad_x = 0u;
	eng::u8 m_pad_y = 0u;
};

} // namespace eng::scene
