#pragma once

/// \file compose.hpp
/// **Composición de escenas** (prototipo del modelo de tres planos): una escena se construye
/// uniendo **etapas** que piden recursos y emiten Copper, en vez de una clase por driver.
/// Ver `docs/engine/architecture/SCENE_COMPOSITION.md`.
///
/// Uso:
///
/// ```cpp
/// composition::Scene s;
/// composition::compose(s, memory, composition::planar4(320, 256),
///     composition::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200),
///     composition::palette(pal, 0, 16));
/// s.install(backend);        // o takeover
/// ```
///
/// Una etapa es un callable `void(Scene&)`: pide lo que necesita a la escena (geometría,
/// planos) y emite al `copper::Scheduler`. Los **presets** son funciones que devuelven un
/// `SceneResources`; no hay clase por efecto. El **programa** es data (Copper) que ejecuta
/// el chip; la **variabilidad** se hace con `copper::PatchHandle` (`scheduler().patchable`).
///
/// ```text
///   escena = unión de ETAPAS (no una clase por driver)
///   ┌──────────────────────────────────────────────────────────────────┐
///   │ composition::Scene                                                       │
///   │  recursos: geometría · planos · buffers · paleta                   │
///   │  ├─ display()        ─┐                                            │
///   │  ├─ palette()        ─┼─► piden recursos y emiten al copper::Scheduler
///   │  ├─ palette_zones()  ─┤                                            │
///   │  └─ reverse_ptrs()   ─┘                                            │
///   │  ciclo de vida: Task = FunctionRef<void()> (por frame)             │
///   │  variabilidad : copper::PatchHandle (scheduler().patchable)        │
///   └──────────────────────────────────────────────────────────────────┘
///   El PROGRAMA es data (Copper) que ejecuta el chip; los presets devuelven un SceneResources.
/// ```

#include <eng/graphics/composition/scene.hpp>
#include <eng/graphics/composition/stages.hpp>
