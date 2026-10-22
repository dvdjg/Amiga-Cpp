#pragma once

/// \file entry.hpp
/// **Arranque de una app/demo Amiga del engine**: el bootstrap de plataforma pone `SysBase`
/// (`amiga_boot.cpp`, constructor previo a `main`), el símbolo `g_eng_run_status` que lee el
/// runner, la memoria del juego, el display que la app declara y el bucle; la app solo
/// escribe su lógica y **fija su display de forma imperativa**.
///
/// ```cpp
/// int main() {
///     eng::GameDisplay display {};        // 320x256x4 por defecto
///     display.width = 320;
///     display.palette = mi_paleta;
///     MyGame game {};
///     return eng::amiga::run_demo(game, display);
/// }
/// ```
///
/// - `run_demo(game, display)`: valida y configura la memoria del juego, arranca el display
///   declarado (`set_display` + `start`), añade el `probe` del runner por dentro y corre el
///   bucle indefinido. Devuelve el código de `main` (los fallos de arranque quedan en el
///   `RunStatus` con `BootFail`).
/// - `ENG_APP_MAIN(Game)`: bootstrap **mínimo** heredado (sin display/memoria automáticos)
///   para composición manual de la escena.
///
/// La app **no** incluye `support/gcc8_c_support.h`, no declara `SysBase` ni
/// `g_eng_run_status` (los define el engine en débil) y no escribe plomería de plataforma
/// (criterios de ergonomía: `CODING_STYLE.md` §Criterios del usuario y `API_ERGONOMICS.md`).
///
/// Es Amiga-específico (instancia `eng::amiga::AmigaBackend`); otro backend tendría su `entry`.

#include <eng/api/game.hpp>
#include <eng/core/util/scope_guard.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp> // `MemoryConfig` para el overload con presupuesto explícito
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/vblank_mode.hpp>

extern "C" {
extern volatile ::eng::debug::RunStatus g_eng_run_status;
}

struct ExecBase;
extern struct ExecBase* SysBase; ///< base de Exec (la define `amiga_boot.cpp`)

namespace eng::amiga {

/// Garantiza que `SysBase` esté inicializado (idempotente). El arranque normal ya lo hace el
/// constructor de `amiga_boot.cpp` antes de `main`; esta llamada cubre enlaces donde los
/// constructores no corren y permite al bootstrap no depender de ello. La app **no** lo
/// escribe: lo llama `run_demo`/`ENG_APP_MAIN`.
inline void ensure_boot() noexcept {
	if (SysBase == nullptr) {
		SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	}
}

namespace detail {

/// Adapta el demo al contrato del `App` y añade la instrumentación del runner
/// (`probe_when_ready`) sin que el juego la escriba ni nombre el `RunStatus`.
template <class Demo>
class DemoHost {
public:
	explicit DemoHost(Demo& demo) noexcept : m_demo(demo) {}

	/// Reenvía el `init` al demo si lo implementa (los hooks son opcionales).
	void init(auto& app) {
		if constexpr (requires { m_demo.init(app); }) {
			m_demo.init(app);
		}
	}
	/// Reenvía el `update` al demo si lo implementa.
	void update(auto& app) {
		if constexpr (requires { m_demo.update(app); }) {
			m_demo.update(app);
		}
	}
	/// Reenvía el `render` al demo y añade el `probe` del runner (sin que el juego lo vea).
	void render(auto& app) {
		if constexpr (requires { m_demo.render(app); }) {
			m_demo.render(app);
		}
		::eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

private:
	Demo& m_demo;
};

} // namespace detail

/// Marca el demo como **listo** (el runner lo lee y captura). Lo llama el demo cuando su
/// puesta en escena está completa; el juego no ve `g_eng_run_status`. `detail` permite publicar
/// un resumen compacto del estado útil para el test/diagnóstico (los contadores completos van
/// por `DebugPeripheral`).
inline void mark_ready(eng::u32 detail = 0u) {
	::eng::debug::mark_ready(g_eng_run_status, detail);
}

/// Marca **fallo controlado** con una etapa tipada (el runner lo lee en el `detail`).
inline void mark_failed(::eng::debug::BootFail stage) {
	::eng::debug::mark_failed(g_eng_run_status, stage);
}

/// **Modo de espera de VBlank** del bucle de `run_demo` (familia 212/DT-011; receta
/// operativa DT-042): con la espera por **señal** el planificador de un SO vivo puede
/// apagar el latido y la tarea duerme para siempre (parón determinista de frames, PC en
/// el `STOP` del dispatcher) — el engine la rechaza con `0xE212`; la espera **activa**
/// (latido por IRQ + sondeo de VPOS) evita el camino de `Wait()` y es la variante
/// recomendada para demos con trabajo por frame variable; **`Polling`** no depende de la
/// IRQ de VBlank en absoluto (`App::run_polling`) y es la única estable para las demos
/// que mueren también con la espera activa (clase 119/400/303: la interacción de las IRQ
/// con exec bajo el SO vivo). Ver
/// `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`.
enum class VblankWait : eng::u8 {
	Signal = 0u, ///< duerme en `Wait()` (por defecto; no usar bajo SO vivo sin takeover)
	Active,      ///< espera activa (`set_vblank_sleep(false)`)
	Polling,     ///< bucle por sondeo (`run_polling`), sin depender de la IRQ de VBlank
	Messages,    ///< cooperativo: display del SO, `AddIntServer` y `Wait` (sin tocar Copper)
};

namespace detail {

/// Cuerpo compartido de `run_demo`: memoria del juego (inválida = perfil de juego), display
/// declarado, `start` y bucle. Los fallos quedan en el `RunStatus` con `BootFail`. La
/// configuración opcional viaja como `eng::Ref` (referencia no propietaria, regla del repo),
/// no como puntero crudo.
template <class Demo>
int run_demo_impl(Demo& demo, const GameDisplay& display, eng::Ref<const MemoryConfig> memory,
		  VblankWait wait) {
	ensure_boot();
	::eng::debug::reset(g_eng_run_status);
	::eng::debug::mark_init_started(g_eng_run_status);

	AmigaBackend backend {};
	// **Modo de espera de VBlank** (familia 212/DT-011, receta DT-042): con la espera por
	// señal el planificador de un SO vivo puede apagar el latido y la tarea duerme para
	// siempre (parón determinista); la espera activa (latido por IRQ + sondeo de VPOS)
	// evita el camino de `Wait()`; el sondeo no depende de la IRQ en absoluto. Se
	// configura ANTES de `start` para que el bucle nazca en el modo.
	const bool cooperative = (wait == VblankWait::Messages);
	eng::amiga::set_vblank_cooperative(cooperative);
	backend.set_vblank_sleep(wait == VblankWait::Signal || cooperative);
	const bool memory_ok = memory.valid() ? backend.configure_memory(*memory)
					      : backend.configure_game_memory();
	if (!memory_ok) {
		::eng::debug::mark_failed(g_eng_run_status, ::eng::debug::BootFail::Memory);
		return 0;
	}
	// **Toma de control del sistema** (procedimiento canónico, ver
	// `docs/reference/amiga/hardware/takeover-y-restauracion-canonica.md`): bajo el runner el
	// proceso corre en un AmigaDOS vivo y `graphics.library`/exec reescriben `INTENA` y apagan
	// VERTB (familia 212/DT-011). `Forbid()` + `LoadView(NULL)` + `OwnBlitter()` son necesarios
	// para que el display del SO deje de tocar la copperlist y los bits de interrupción. Se
	// excluye `Signal`: `Wait()` con `Forbid()` activo puede bloquear la tarea para siempre;
	// las demos con `Signal` deben migrar a `Active`/`Polling` antes de recibir el takeover.
	// **Exclusive** (`Active`/`Polling`): display propio + `Forbid` + AddIntServer; sin `Wait`.
	// **Cooperative** (`Messages`): display del SO, `AddIntServer`, `Wait`/`WaitPort`; **no** se
	// toca Copper/DMACON ni se hace takeover (KS 1.3 no permite ambas cosas a la vez).
	const bool system_takeover = (wait == VblankWait::Active || wait == VblankWait::Polling);
	if (system_takeover && !backend.takeover_system()) {
		::eng::debug::mark_failed(g_eng_run_status, ::eng::debug::BootFail::Display);
		return 0;
	}
	[[maybe_unused]] auto system_restore = eng::util::make_scope_guard([&] {
		if (system_takeover) backend.restore_system();
	});
	DemoHost<Demo> host {demo};
	::eng::App app {backend, host, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		::eng::debug::mark_failed(g_eng_run_status, ::eng::debug::BootFail::Display);
		return 0;
	}
	if (wait == VblankWait::Polling) {
		app.run_polling();
	} else {
		app.run();
	}
	return 0;
}

} // namespace detail

/// **Bootstrap completo** de una app con fachada: memoria del juego, display declarado por el
/// llamador, `start` y bucle indefinido. Los fallos de arranque quedan en el `RunStatus` con
/// `BootFail`; devuelve 0 (el runner mira el estado, no el código de salida).
/// \param demo     instancia del juego/demo (`init/update/render` con `auto&` u `App&`).
/// \param display  descripción imperativa del display (`GameDisplay`, con sus campos).
template <class Demo>
int run_demo(Demo& demo, const GameDisplay& display) {
	return detail::run_demo_impl(demo, display, {}, VblankWait::Signal);
}

/// Igual, con el **modo de espera de VBlank** explícito (ver `VblankWait`).
template <class Demo>
int run_demo(Demo& demo, const GameDisplay& display, VblankWait wait) {
	return detail::run_demo_impl(demo, display, {}, wait);
}

/// Igual, con un **presupuesto de memoria explícito** (`MemoryConfig` por banco) para apps
/// que no usan el perfil de juego por defecto.
template <class Demo>
int run_demo(Demo& demo, const GameDisplay& display, const MemoryConfig& memory) {
	return detail::run_demo_impl(demo, display, eng::Ref<const MemoryConfig> {memory},
				     VblankWait::Signal);
}

/// Igual, con memoria explícita y modo de espera de VBlank.
template <class Demo>
int run_demo(Demo& demo, const GameDisplay& display, const MemoryConfig& memory, VblankWait wait) {
	return detail::run_demo_impl(demo, display, eng::Ref<const MemoryConfig> {memory}, wait);
}

} // namespace eng::amiga

/// **Bootstrap mínimo** heredado: `main()` con un `App` sobre el `Game` tal cual (sin
/// display ni memoria automáticos), para quien compone su escena a mano. `SysBase` y el
/// `RunStatus` los pone el engine.
#define ENG_APP_MAIN(GameType)                                                                 \
	int main() {                                                                           \
		::eng::amiga::ensure_boot();                                                   \
		::eng::debug::reset(g_eng_run_status);                                         \
		::eng::amiga::AmigaBackend backend {};                                         \
		GameType game {};                                                              \
		::eng::App app {backend, game};                                                \
		app.run();                                                                     \
		return 0;                                                                      \
	}
