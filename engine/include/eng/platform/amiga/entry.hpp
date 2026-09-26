#pragma once

/// \file entry.hpp
/// **Arranque de una app Amiga del engine**: `ENG_APP_MAIN(Game)` escribe `main()` por ti
/// (bootstrap de `SysBase`, el `g_eng_run_status` que lee el runner, el backend y `App::run()`),
/// de modo que el juego no repite ese boilerplate.
///
/// ```cpp
/// struct MyGame { void init(eng::App&); void update(eng::App&); void render(eng::App&); };
/// ENG_APP_MAIN(MyGame);
/// ```
///
/// Es Amiga-específico (instancia `eng::amiga::AmigaBackend`); otros backends tendrían su propia
/// `entry.hpp`.

#include <eng/api/game.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

/// El runner lee este símbolo del `.map`; `ENG_APP_MAIN` lo **define**.
extern "C" {
extern volatile ::eng::debug::RunStatus g_eng_run_status;
}

#ifndef ENG_APP_MAIN
/// Define `SysBase`, `g_eng_run_status` y `main()` para una app de un solo `Game`.
#define ENG_APP_MAIN(GameType)                                                                 \
	struct ExecBase* SysBase = nullptr;                                                    \
	extern "C" {                                                                           \
	__attribute__((used)) volatile ::eng::debug::RunStatus g_eng_run_status {               \
		::eng::debug::run_status_magic, ::eng::debug::run_status_version,               \
		static_cast<::eng::u16>(::eng::debug::RunState::Cold), 0, 0,                    \
	};                                                                                     \
	}                                                                                      \
	int main() {                                                                           \
		SysBase = *reinterpret_cast<struct ExecBase**>(4UL);                           \
		::eng::debug::reset(g_eng_run_status);                                         \
		::eng::amiga::AmigaBackend backend {};                                         \
		GameType game {};                                                              \
		::eng::App app {backend, game};                                                \
		app.run();                                                                     \
		return 0;                                                                      \
	}
#endif
