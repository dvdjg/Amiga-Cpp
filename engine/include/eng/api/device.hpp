#pragma once

/// \file device.hpp
/// **Servicios de hardware del juego** (`eng::Device`): agrupa lo que no es alto nivel
/// —memoria, Blitter, Copper, raster— para que `App` quede como *composition root* corto y el
/// juego pida hardware **por intención** sin nombrar el backend. Se obtiene con `app.device()`.
///
/// Cada método reenvía al backend si lo soporta (`requires`); si no, es no-op (`false`). El
/// juego simple puede **ignorar** `device()` por completo. Ver
/// `docs/guides/roadmap/ROADMAP_API_COHERENCE.md` (F2) y `PUBLIC_GAME_API.md` §5/12.

#include <eng/api/copper.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng {

/// Vista de servicios de hardware sobre un backend y la escena ligada (no propietaria).
/// \tparam Backend backend de plataforma usado por la fachada
template <class Backend>
class Device {
public:
	constexpr Device(Backend& backend, eng::Ref<graphics::composition::Scene> scene) noexcept
		: m_backend(backend), m_scene(scene) {}

	// --- Memoria ----------------------------------------------------------------------
	/// **Bancos tipados** del backend: la puerta de reserva **persistente** del engine
	/// (`memory_manager().chip().reserve<Tag>(...)`). El juego no toca las arenas: los recursos
	/// persistentes van por banco (`WorksTag`/`PlaneTag`/…) y la scratch de frame la gestiona el
	/// engine (`reset_frame`). Ver `MEMORY_OWNERSHIP.md`.
	/// \tparam B tipo del backend de plataforma
	template <class B = Backend>
	[[nodiscard]] decltype(auto) memory_manager() {
		return m_backend.memory_manager();
	}

	// --- Blitter ----------------------------------------------------------------------
	/// \tparam B tipo del backend de plataforma
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
	/// \tparam B tipo del backend de plataforma
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
	/// \param dst          base de los planos (Chip).
	/// \param planes       nº de planos a borrar.
	/// \param row_bytes    bytes por fila de un plano.
	/// \param plane_bytes  separación entre planos.
	/// \param w,h          rectángulo en píxeles.
	/// \param wait         `true` espera al Blitter; `false` lo deja en vuelo.
	/// \return `false` si el backend no lo soporta.
	/// \tparam B tipo del backend de plataforma
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
	/// \param bobs           lote de BOBs (orden de dibujo).
	/// \param words          ancho de cada BOB en palabras.
	/// \param height         alto en píxeles.
	/// \param source_modulo  `BLTAMOD`/`BLTBMOD` (bytes).
	/// \param dest_modulo    `BLTDMOD` (bytes; normalmente = `source_modulo`).
	/// \return `false` si el backend no lo soporta.
	/// \tparam B tipo del backend de plataforma
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
	/// \tparam B tipo del backend de plataforma
	template <class B = Backend>
	bool blitter_or_bobs(const graphics::OrBob& bob, u16 words, u16 height, s16 source_modulo,
			     s16 dest_modulo) {
		return blitter_or_bobs<B>(eng::Span<const graphics::OrBob> {&bob, 1u}, words, height,
					  source_modulo, dest_modulo);
	}

	/// **Colisión pixel-perfect** por Blitter: `scratch = a & b` por plano y `true` si hay
	/// algún bit. `words`×`rows` es el rect en palabras de 16 px × filas.
	/// \param a,b         planos a comprobar (`A` y `B`).
	/// \param scratch     planos de trabajo para el `a & b`.
	/// \param planes      nº de planos.
	/// \param row_bytes   bytes por fila de un plano.
	/// \param plane_bytes separación entre planos.
	/// \param words,rows  rectángulo (palabras de 16 px × filas).
	/// \return `true` si hay algún bit común (colisión).
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
	/// \param plan  el plan del frame (blits encolados).
	/// \return `false` si el backend no ejecuta planes.
	/// \tparam B tipo del backend de plataforma
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
	/// \param plan  el programa de Copper a instalar.
	/// \return `false` si el programa/backend no lo permite.
	/// \tparam B tipo del backend de plataforma
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

	/// Instala una copperlist **estática** propia en Chip (`takeover`, una vez en `init`).
	/// Es el camino de mínima CPU: el driver construye la estructura una vez y por frame solo
	/// parchea palabras de dato con `copper::patch_data`.
	/// \tparam B tipo del backend de plataforma
	template <class B = Backend>
	bool takeover_copper(eng::ChipView<eng::CopperTag> list) {
		if constexpr (requires(B& b, eng::ChipView<eng::CopperTag> v) { b.takeover_display(v); }) {
			if (list.empty()) return false;
			m_backend.takeover_display(list);
			return true;
		} else {
			(void)list;
			return false;
		}
	}

	/// Publica una copperlist estática ya construida (swap de `COP1LC`). El Copper recarga el
	/// puntero al wrap vertical, así que el llamador puede terminar la lista inactiva y publicarla
	/// sin modificar words que el DMA podría estar leyendo (`WinUAE-DBG/custom.cpp:2921-2942,
	/// `11237-11245`; AHRM 3.ª cap. 2, `COP1LC`/`COPJMP1`). Es el camino de los drivers que
	/// cambian de estructura sin reemitirla cada frame.
	/// \tparam B tipo del backend de plataforma
	template <class B = Backend>
	bool commit_copper(eng::ChipView<eng::CopperTag> list) {
		if constexpr (requires(B& b, eng::ChipView<eng::CopperTag> v) { b.install_copper_list(v); }) {
			if (list.empty()) return false;
			m_backend.install_copper_list(list);
			return true;
		} else {
			(void)list;
			return false;
		}
	}

	/// Instala una copperlist de doble buffer propia (una vez, en `init`). Se usa cuando un
	/// driver del juego recompone directamente un programa ordenado en cada frame y no necesita
	/// el arbitraje de intenciones de `copper::Plan`.
	/// \tparam B tipo del backend de plataforma
	template <class B = Backend>
	bool takeover_copper(eng::copper::DoubleBuffer& lists) {
		if constexpr (requires(B& b, eng::ChipView<eng::CopperTag> v) { b.takeover_display(v); }) {
			if (!lists.ok()) return false;
			lists.takeover(m_backend);
			return true;
		} else {
			(void)lists;
			return false;
		}
	}

	/// Publica un programa de Copper propio (swap de `COP1LC`, tras VBlank).
	/// \param plan  el programa de Copper ya emitido.
	/// \return `false` si el backend no lo publica.
	/// \tparam B tipo del backend de plataforma
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

	/// Publica la lista activa de un doble buffer ya construido (swap de `COP1LC` en VBlank).
	/// \tparam B tipo del backend de plataforma
	template <class B = Backend>
	bool commit_copper(eng::copper::DoubleBuffer& lists) {
		if constexpr (requires(B& b, eng::ChipView<eng::CopperTag> v) { b.install_copper_list(v); }) {
			if (!lists.ok()) return false;
			lists.install(m_backend);
			return true;
		} else {
			(void)lists;
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
	/// **Fachada de Copper** de alto nivel (por intención) sobre el scheduler de la escena.
	[[nodiscard]] Copper copper_builder() noexcept {
		return Copper {m_scene.get()->scheduler()};
	}

private:
	Backend& m_backend;
	eng::Ref<graphics::composition::Scene> m_scene {};
};

} // namespace eng
