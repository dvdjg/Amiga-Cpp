# Modelo de efectos (diseño)

Objetivo: que un juego/demo pida **efectos** y los componga, sin orquestar primitivas de
Copper ni conocer planos/`FramePlan`. Este documento fija la interfaz, la composición en la
escena, la propiedad de banda y el presupuesto por efecto. Parte de lo que **ya existe**; no
introduce un motor nuevo.

## 1. Qué hay hoy

| Pieza | Dónde | Nota |
|---|---|---|
| `concept Effect<E, Plan, Tick>` | `graphics/raster_intent.hpp` | `e.update(tick)` + `e.apply_into(plan)` |
| `PaletteCycleEffect` / `PaletteTransitionEffect` | `graphics/effects/` | cumplen `Effect<E, FramePlan>` (`static_assert`) |
| `RasterGradientEffect` | `graphics/effects/raster_gradient.hpp` | produce `CopperIntent`; `apply_into(plan)` |
| `effects::CopperChunky` | `api/effects.hpp` | efecto de alto nivel con ciclo propio |
| `Scene` | `graphics/composition/compose.hpp` | `begin_build`/`end_build`, `plan()`, `scheduler()`, `surface()`, `on_frame(Task)` |

Es decir: **el contrato de efecto existe** (`Effect`) y hay efectos que lo cumplen, pero la
**composición** es manual: cada `main.cpp` llama a los efectos desde su `on_frame` y los
registra en el `Plan` a mano. `Scene::on_frame` admite **una sola** `Task`.

## 2. Interfaz

Se mantiene el `concept Effect` (cero coste, sin virtuals). El tipo `Plan` decide la
**familia** del efecto:

```cpp
// Display: aporta intenciones de Copper (bars, gradientes, splits, chunky).
template <typename E> concept DisplayEffect = Effect<E, copper::Plan>;
// Render: aporta trabajos de render (BlitJobs, parches de paleta) al plan del frame.
template <typename E> concept RenderEffect  = Effect<E, graphics::FramePlan>;
```

Un efecto no escribe hardware: `apply_into` solo registra intenciones/trabajos.

## 3. Composición en la escena

La escena pasa a llevar una **lista ordenada de efectos** y los ejecuta en su ciclo:

```
   Scene::tick(frame):
       begin_build();                     // retarget del emisor al bloque trasero
       for (e in effects) {               // orden de registro = orden de aportación
           e.update(frame);
           e.apply_into(plan());          // o el FramePlan del frame
       }
       end_build();                       // ordena por scanline + presupuesto + flip
```

Sin heap ni virtuals: la lista son `eng::util::FunctionRef<void(Scene&)>` (el mismo
mecanismo que `Task`), o un número fijo de *slots* tipados si se quiere evitar el *type
erasure*.

> **El `FunctionRef` no es propietario**: el callable debe **sobrevivir** a la escena. Pasar una
> **lambda temporal** a `add_effect` (o a `on_frame`) deja una referencia colgante. El patrón es
> un **functor miembro** del juego (como `FrameTask` en la demo `081_background_tasks`).

```cpp
struct SkyEffect {                 // functor miembro: vive tanto como la escena
    Game* self = nullptr;
    void operator()(Scene& s) const { self->sky.set_phase(s.frame()); self->sky.frame(s); }
};

eng::effects::Gradient sky;
sky.attach({.first_line = 0x2c, .band_height = 8, .bands = 24, .first = 0}, keys);
m_sky_effect.self = this;
scene.add_effect(m_sky_effect);    // NO `[&](Scene& s) { ... }` (temporal -> cuelga)
```

## 4. Propiedad de banda y resolución de conflictos

Hoy el `Plan` ordena las intenciones por scanline y, ante solape, **el último gana** sin
política. Un efecto declara el tramo y los registros que toca:

```cpp
struct EffectScope {
    u16 first_line = 0;
    u16 last_line  = 0;   // inclusivo
    u16 register_mask = 0; // bits = COLORxx/registros reclamados (0 = cualquiera)
};
```

Al registrar, la escena (o el `Plan`) acumula los tramos y **detecta el solape**:
`add_effect(e, scope)` devuelve `false`/motivo si dos efectos reclaman la misma línea y el
mismo registro. Es la diferencia entre «conflicto silencioso» y «falla clara».

## 5. Presupuesto por efecto

El presupuesto actual es global (por línea, `Timeline`, y `max_intents` fijo). Para que un
efecto pueda fallar **temprano** y para saber **quién** consume:

```cpp
struct EffectCost { u16 intents = 0; u16 words = 0; };  // huella declarada
```

Cada efecto expone `cost()` (o `intents_words()` de `compose.hpp` para las intenciones que
emite). El `Plan` **suma** y compara con `copper_bytes`; `end_build()` puede devolver qué
efecto agotó el presupuesto en vez de un `false` opaco.

## 6. Migración (evolutiva)

1. **Envolver lo existente** como efectos de API: `effects::Gradient` (sobre
   `RasterGradientEffect`), `effects::Rotozoom`, `effects::SpriteLayer`;
   `effects::CopperChunky` ya está.
2. **`Scene::add_effect` + `Scene::tick`** que ejecute la lista (hoy `on_frame` = 1 tarea).
3. **Reserva de banda** en el `Plan` (§4) y **coste por efecto** (§5).
4. **Validar** en host y en las demos 125/083 antes de migrar más.

## 7. Límites y decisiones

- **`Effect` es un concept, no una base**: cero coste, sin virtuals, testeable en host.
- **Sin heap**: registro por `FunctionRef` o slots fijos.
- **El `Plan` no cambia de motor**: solo gana reserva de banda y contabilidad.
- **No todo es `DisplayEffect`**: los efectos de píxel (rotozoom, vectores) son
  `RenderEffect` sobre `FramePlan`; el mismo concept los cubre.

## 8. Referencias

- `engine/include/eng/graphics/raster_intent.hpp` — `concept Effect`
- `engine/include/eng/graphics/copper/plan.hpp` — orden, presupuesto, materialize
- `engine/include/eng/api/effects.hpp` — efectos de alto nivel (`CopperChunky`)
- [PUBLIC_GAME_API.md](PUBLIC_GAME_API.md) §5.3 (efectos como concepto)
