# Tutorial 01 — Hola mundo

El programa mínimo del engine: arranca el backend, deja que el `App` componga el display, pinta algo
y publica el frame. Al terminar sabrás el esqueleto de **cualquier** app.

## El esqueleto

```cpp
// main.cpp
#include <eng/api/api.hpp>                       // la fachada (un solo include)
#include <eng/platform/amiga/backend.hpp>        // el backend (solo aquí)

#include <exec/execbase.h>
#include <proto/exec.h>
#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

// Telemetría del arranque: el runner la lee por el canal lateral (InitStarted/Ready/Failed).
extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

struct HolaMundo {
	// Una vez: aquí se reservan recursos y se describe la escena.
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_fondo = app.device().memory_manager().chip().reserve<eng::PlaneTag>(40u * 256u, 16u);
		eng::debug::mark_ready(g_eng_run_status, 0x01000000u);
	}

	// Cada frame: lógica (no dibuja).
	void update(auto& app) { eng::debug::mark_frame(g_eng_run_status, app.frame()); }

	// Cada frame: dibujo y publicación.
	void render(auto& app) {
		app.screen().clear(0);                                  // color 0 de la paleta
		app.screen().fill(eng::Box {16, 16, 288, 96}, 1);       // un rectángulo de color 1
		app.screen().frame(eng::Box {16, 16, 288, 96}, 2);      // marco de color 2
		app.present();                                          // ejecuta el plan + publica
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}

private:
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_fondo {};
};

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};               // el backend, una vez y solo aquí
	if (!backend.configure_memory({128u * 1024u, 8u * 1024u, 4u * 1024u})) return 0;

	eng::GameDisplay display {};                       // qué display queremos
	display.width = 320;
	display.height = 256;
	display.color_depth = 3;                            // 3 planos = 8 colores

	HolaMundo game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) return 0;   // compone y toma el display
	app.run(0xffffu);                                  // bucle por VBlank
	return 0;
}
```

## Qué hace cada pieza

| Pieza | Qué es |
|---|---|
| `#include <eng/api/api.hpp>` | La **fachada**: `App`, `Screen`, `Box`, `Palette`, errores… |
| `eng::amiga::AmigaBackend backend {}` | El **backend**: memoria, Blitter, Copper, VBlank. Solo en `main()`. |
| `backend.configure_memory({...})` | Reserva los bancos **Chip, Slow, Frame** (en bytes). Ver [modelo de memoria](../05_arquitectura/modelo_de_memoria.md). |
| `eng::GameDisplay` | Descripción del display (ancho/alto/planos/paleta). |
| `eng::App app {backend, game, backend.memory_manager()}` | El **composition root**: bucle + servicios. |
| `app.set_display(display)` + `app.start()` | Compone la escena y **toma el display**. |
| `app.run(0xffff)` | Bucle por VBlank; `0xffff` = muchos frames (el runner captura y cierra). |
| `Game::init/update/render` | Tu contrato: recursos una vez, lógica por frame, dibujo por frame. |
| `g_eng_run_status` + `mark_*` | Telemetría: el runner sabe si llegaste a `Ready`. |

## Reglas que ya estás siguiendo (sin darte cuenta)

- **El backend no sale de `main()`**: `init/update/render` reciben `app` (fachada), no `backend`.
- **La memoria entra tipada**: `reserve<eng::PlaneTag>` (no un `u8*`).
- **`update` no dibuja**: el dibujo va en `render`, y se **publica** con `present()`.
- **Telemetría obligatoria**: `mark_init_started` → `mark_ready` (y `mark_frame` por frame).

## Cómo compilar y ejecutar

```bash
# Windows nativo + Git Bash (ver docs/build/BUILD_AND_RUN.md):
bash ./tools/build/build-demo.sh <tu-demo> --debug
bash ./tools/run/run-demo.sh   <tu-demo>
```

Coloca el `main.cpp` y el `README.md` en `demos/<familia>/<categoría>/NNN_tema/src/main.cpp`.

## Siguiente

[Tutorial 02 — Bucle, VBlank y frame](02_bucle_vblank_y_frame.md): por qué `update` va después del
VBlank y cómo encaja el diseño. Ejemplo vivo: `demos/features/engine/amiga/061_indexed_display`
(que además dibuja un framebuffer indexado).

Volver a [Tutoriales](README.md) · [índice del manual](../README.md).
