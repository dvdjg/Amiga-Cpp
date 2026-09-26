#pragma once

/// \file device.hpp
/// **Servicios de hardware del juego** (`eng::Device`): agrupa lo que no es alto nivel
/// —memoria, Blitter, Copper, raster— para que `App` quede como *composition root* corto y el
/// juego pida hardware **por intención** sin nombrar el backend. Se obtiene con `app.device()`.
///
/// Cada método reenvía al backend si lo soporta (`requires`); si no, es no-op (`false`). El
/// juego simple puede **ignorar** `device()` por completo. Ver
/// `docs/engine/architecture/ROADMAP_API_COHERENCE.md` (F2) y `PUBLIC_GAME_API.md` §5/12.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng {

/// Vista de servicios de hardware sobre un backend y la escena ligada (no propietaria).
template <class Backend>
class Device {
public:
	constexpr Device(Backend& backend, eng::Ref<graphics::composition::Scene> scene) noexcept
		: m_backend(backend), m_scene(scene) {}

	// --- Memoria ----------------------------------------------------------------------
	template <class B = Backend>
	[[nodiscard]] decltype(auto) memory() {
		return m_backend.memory();
	}

	// --- Blitter ----------------------------------------------------------------------
	template <class B = Backend>
	bool wait_blitter() {
		if constexpr (requires(B& b) { b.wait_blitter(); }) {
			return m_backend.wait_blitter();
		} else {
			return false;
		}
	}

	/// Instala el **rasterizador por Blitter** de la escena (los rellenos de `screen()` usan
	/// el Blitter). `false` si el backend no lo soporta.
	template <class B = Backend>
	bool install_raster(graphics::composition::Scene& scene) {
		if constexpr (requires(B& b, graphics::composition::Scene& s) {
				      b.install_raster(s);
			      }) {
			m_backend.install_raster(scene);
			return true;
		} else {
			(void)scene;
			return false;
		}
	}

	/// Borra `planes` planos (`w`×`h`, `D = 0`). `wait` sincroniza con el fin del blit.
	template <class B = Backend>
	bool blitter_clear(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w,
			   u16 h, bool wait = true) {
		if constexpr (requires(B& b, eng::PlaneBytes d) {
				      b.blitter_clear(d, u8 {}, u16 {}, u32 {}, u16 {}, u16 {}, bool {});
			      }) {
			return m_backend.blitter_clear(dst, planes, row_bytes, plane_bytes, w, h, wait);
		} else {
			(void)dst;
			(void)planes;
			(void)row_bytes;
			(void)plane_bytes;
			(void)w;
			(void)h;
			(void)wait;
			return false;
		}
	}

	/// **BOBs OR en lote** (`D = A | D`) en orden; `source_modulo`/`dest_modulo` son
	/// `BLTAMOD`/`BLTBMOD`=`BLTDMOD`.
	template <class B = Backend>
	bool blitter_or_bobs(eng::Span<const graphics::OrBob> bobs, u16 words, u16 height,
			     s16 source_modulo, s16 dest_modulo) {
		if constexpr (requires(B& b, eng::Span<const graphics::OrBob> o) {
				      b.blitter_or_bobs(o.data(), u32 {}, u16 {}, u16 {}, s16 {},
							s16 {});
			      }) {
			return m_backend.blitter_or_bobs(bobs.data(),
							 static_cast<u32>(bobs.size()), words, height,
							 source_modulo, dest_modulo);
		} else {
			(void)bobs;
			(void)words;
			(void)height;
			(void)source_modulo;
			(void)dest_modulo;
			return false;
		}
	}

	/// **Un solo BOB OR** (azúcar del anterior, sin envolver en `Span` a mano).
	template <class B = Backend>
	bool blitter_or_bobs(const graphics::OrBob& bob, u16 words, u16 height, s16 source_modulo,
			     s16 dest_modulo) {
		return blitter_or_bobs<B>(eng::Span<const graphics::OrBob> {&bob, 1u}, words, height,
					  source_modulo, dest_modulo);
	}

	/// **Colisión pixel-perfect** por Blitter: `scratch = a & b` por plano y `true` si hay
	/// algún bit. `words`×`rows` es el rect en palabras de 16 px × filas.
	template <class B = Backend>
	bool blitter_collide(eng::PlaneBytes a, eng::PlaneBytes b, eng::PlaneBytes scratch, u8 planes,
			     u16 row_bytes, u32 plane_bytes, u16 words, u16 rows) {
		if constexpr (requires(B& bk, eng::PlaneBytes p) {
				      bk.blitter_collide(p, p, p, u8 {}, u16 {}, u32 {}, u16 {}, u16 {});
			      }) {
			return m_backend.blitter_collide(a, b, scratch, planes, row_bytes, plane_bytes,
							 words, rows);
		} else {
			(void)a;
			(void)b;
			(void)scratch;
			(void)planes;
			(void)row_bytes;
			(void)plane_bytes;
			(void)words;
			(void)rows;
			return false;
		}
	}

	/// Ejecuta un `FramePlan` (jobs de Blitter) en el backend.
	template <class B = Backend>
	bool execute_frame_plan(graphics::FramePlan& plan) {
		if constexpr (requires(B& b, const graphics::FramePlan& p) { b.execute_frame_plan(p); }) {
			return m_backend.execute_frame_plan(plan);
		} else {
			(void)plan;
			return false;
		}
	}

	// --- Copper -----------------------------------------------------------------------
	/// Instala un programa de Copper propio (`copper::Plan`) en el backend (una vez, en `init`).
	template <class B = Backend>
	bool takeover_copper(copper::Plan& plan) {
		if constexpr (requires(B& b, copper::Plan& p) { p.takeover(b); }) {
			plan.takeover(m_backend);
			return true;
		} else {
			(void)plan;
			return false;
		}
	}

	/// Publica un programa de Copper propio (swap de `COP1LC`, tras VBlank).
	template <class B = Backend>
	bool commit_copper(copper::Plan& plan) {
		if constexpr (requires(B& b, copper::Plan& p) { p.commit(b); }) {
			plan.commit(m_backend);
			return true;
		} else {
			(void)plan;
			return false;
		}
	}

	/// Escena ligada (para efectos avanzados que el `Screen` no cubre).
	[[nodiscard]] graphics::composition::Scene& scene() noexcept { return *m_scene.get(); }
	/// Programa de Copper de la escena ligada.
	[[nodiscard]] copper::Plan& copper() noexcept { return m_scene.get()->plan(); }
	[[nodiscard]] copper::Scheduler& copper_scheduler() noexcept {
		return m_scene.get()->scheduler();
	}

private:
	Backend& m_backend;
	eng::Ref<graphics::composition::Scene> m_scene {};
};

} // namespace eng
