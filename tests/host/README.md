# Batería de tests HOST del engine (algoritmos y APIs puras)

Estos tests compilan **con g++ del host** (el compilador GCC del entorno de
desarrollo del toolchain Amiga, no MSVC) contra `engine/include` y se ejecutan
como un binario nativo de la máquina de desarrollo. Sirven para validar
**algoritmos y APIs puras** (matemáticas, ordenación, tablas, etc.) que no
dependen de hardware y no necesitan WinUAE.

> **Motivación.** El engine se compila para un cruce `m68k-*` (Bartman/VSCode o
> el toolchain del proyecto), pero las cabeceras de `eng/core` son freestanding:
> no usan STL ni dependen del backend. Probarlas con g++ del host es lo más
> rápido y determinista para detectar regresiones en algoritmos puros.

## Dónde está y cómo corre

- Los tests viven en `tests/host/NNN_<nombre>/` con `src/main.cpp`, `README.md`.
- Se compilan con `tools/run-host-tests.sh` (usa `g++` del PATH o la variable
  `CXX`).
- No necesitan `g_eng_run_status` ni canal lateral: terminan con código de
  salida 0 (OK) o distinto de 0 (fallo) y escriben un informe por stdout.

## Convenciones

- El código de los algoritmos que se validan vive en `engine/include/eng/core/`
  y es freestanding (sin STL). Solo el `main.cpp` del test puede usar `printf`
  del host para informar.
- Cada test declara al principio las funciones/buenas prácticas que ejercita.
- Falla con mensaje claro si una aserción no se cumple; el script de regresión
  puede invocar estos binarios y considerar fallo `exit != 0`.

## Cómo añadir un test

1. Elige el siguiente número en `tests/host/NNN_<nombre>/`.
2. Escribe `src/main.cpp` que `#include <eng/core/...>` y aserta los valores.
3. Añade el `README.md`.
4. Regístralo en el `README.md` de esta carpeta (catálogo) y, si la API que
   cubre sube a `engine/`, enlázalo también desde el doc-map del sistema.

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-000 | [eng_core_math](000_eng_core_math/README.md) | `eng::core::isqrt`, `eng::core::quick_sort`, `eng::core::sort_items`, `Span` (port de `libmisc` de `demoscene-repo-orig`). |
| HOST-001 | [graphics_driver_contract](001_graphics_driver_contract/README.md) | Conceptos `eng::DisplayDriver` y `eng::GraphicsDriver` del ciclo de instalación del display (takeover + install): validación compile-time con `static_assert` sobre `StaticEhbScene`, `TileScrollScene`, `XlimitedScene` y compositores DPF. |
| HOST-002 | [raster_intent](002_raster_intent/README.md) | Vocabulario portable de intenciones de display: `Visual`, `CopperIntent`, `SpriteIntent` y concept `Effect` (base de la Oleada 1 de demoscene). |
| HOST-003 | [sprite_allocator](003_sprite_allocator/README.md) | `eng::graphics::SpriteAllocator`: reparto de `SpriteIntent` entre 8 canales con multiplexado vertical y decisión overflow → BOB (paso 4 de `ENGINE_DESIGN.md` §5). |
| HOST-004 | [input](004_input/README.md) | `eng::input::InputAggregator`: estado portable de entrada (pad CD32, ratón, teclado) — paso 6 de `ENGINE_DESIGN.md` §5. |
| HOST-005 | [audio](005_audio/README.md) | `eng::audio::AudioMixer`: asignación de canales de Paula (SampleEvent → AudioPlan) — paso 7 de `ENGINE_DESIGN.md` §5. |
| HOST-006 | [input_decode](006_input_decode/README.md) | `eng::amiga::decode_joystick`: decodificación de los bits de `JOYxDAT` en direcciones (AHRM cap. 8) — backend de entrada. |
| HOST-007 | [input_cd32](007_input_cd32/README.md) | `eng::amiga::decode_cd32_buttons`: decodificación del flujo serie CD32 (9 bits → botones) — backend de entrada. |
| HOST-008 | [game_audio](008_game_audio/README.md) | `eng::audio::SampleBank` + `allow_trigger`: banco de muestras y política de voces (cooldown + límite de instancias) — capa de audio de juego. |
| HOST-009 | [wave_tables](009_wave_tables/README.md) | `eng::audio::sine_byte`/`triangle_byte`/`square_byte`: tablas de forma de onda 8-bit (enteras, sin float). |
| HOST-010 | [math2d](010_math2d/README.md) | `eng::math2d`: primitivas 2D (port de `lib2d`) — MUL/DIV/seno/coseno/atan2 fijos. |
| HOST-011 | [math3d](011_math3d/README.md) | `eng::math3d`: primitivas 3D (port de `lib3d`) — matrices, rotaciones, proyección. |
| HOST-012 | [assets_uaf](012_assets_uaf/README.md) | `eng/assets/uaf.hpp`: contenedor UAF-R (`Blob`/`Reader`/vistas) — bind/find/data + errores. |
| HOST-013 | [math3d_mesh](013_math3d_mesh/README.md) | `eng::math3d` mesh: `mesh_transform`, `mesh_painter_order` (culling + painter). |
| HOST-014 | [object3d](014_object3d/README.md) | `eng::object3d`: port 1:1 de `lib3d` (`Object3D`, transform, aristas/caras). |
| HOST-015 | [fire_sim](015_fire_sim/README.md) | Fuego de `fire-rgb`: simulación (abajo caliente) + `dualtab` C++23 `constexpr` verificado contra el original. |
| HOST-016 | [ham_scene](016_ham_scene/README.md) | `eng::graphics::drivers::HamScene`: display planar con repetición de filas (cuadruplicado) — geometría de la copperlist y parametricidad. |
| HOST-017 | [background_task](017_background_task/README.md) | `eng::task::BackgroundQueue`: tareas de fondo cooperativas (progreso/rendimiento, adaptación por `vpos`, prioridad al bucle principal). |
| HOST-018 | [rtc](018_rtc/README.md) | `eng::time::from_tod`: reloj de tiempo real desde el contador TOD de la CIA-A (50/60 Hz, wrap 24 h). |
