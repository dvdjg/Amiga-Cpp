# El API de juego ideal: dos niveles (alto nivel por defecto, escape close-to-the-metal)

Este documento fija la **intención** del API que consume quien hace un juego para Amiga 500 (y, por
extensión, cualquier plataforma del engine). Los **principios** generales están en
[PUBLIC_API.md](PUBLIC_API.md) §1.1 y la **guía de módulo a módulo** (qué existe y qué falta) en
[PUBLIC_GAME_API.md](PUBLIC_GAME_API.md); aquí se describe la **forma** que debe tener todo el API:
un desarrollador sin conocimiento del chipset expresa **casi cualquier cosa** con vocabulario de
juego, y **solo si quiere** baja al detalle de máquina.

## 1. Objetivo y criterio de éxito

Un programador que **nunca ha tocado un Amiga** debe poder escribir un juego completo —mover
sprites, hacer parallax, poner un HUD, un efecto por raster, cargar música— **sin saber qué es un
bitplane, un `BPLCON`, el Copper, el Blitter, un módulo `BPL1MOD` o un puntero de plano**. El engine
decide **cómo** se materializa en el chipset.

El criterio de éxito no es «se puede», sino **«se puede sin nombrar la máquina»**: si para lograr
algo el desarrollador tiene que aprender un registro o un stride, el API ha fallado en ese punto.

```
   El dev describe QUÉ quiere (dominio)          El engine decide CÓMO (chipset)
   ─────────────────────────────────────         ────────────────────────────────
   "una capa de fondo que scrollee"      ───►     DPF / XLimited / blits / Copper
   "este sprite en (x,y)"                ───►     BOB cookie-cut o sprite hardware
   "un degradado en las líneas 200-240"  ───►     WAIT + MOVE de COLORxx
```

## 2. Los dos niveles (revelación progresiva)

El API tiene **dos niveles**, y cada capacidad vive en el suyo:

- **Nivel A — alto nivel (por defecto, el 90 % del juego).** Vocabulario de juego, sin hardware:
  `Screen`, `Sprite`, `Layer`, `Camera`, `Effect`, `Assets`, `Input`, `Audio`. La llamada habitual
  cabe en una línea y **no expone punteros, offsets, planos, módulos ni registros**.
- **Nivel B — escape deliberado (close-to-the-metal).** Cuando el desarrollador quiere control
  fino (un truco por línea, un layout a medida, depurar), puede bajar a las capas internas del
  engine (`Scene`/`FramePlan`/`RasterLayout`/`Band`/`Scheduler`/`PlayfieldHardwareView`/registros).
  Es **el mismo engine**, no una capa paralela ni un modo «experto» aparte.

Principio rector: **nada se esconde para siempre; todo está disponible, pero cada cosa tiene su
nivel.** El nivel A no es una caja negra opaca: es una fachada **completa** (no recorta) y **fina**
(deja bajar) sobre el nivel B.

```
   ┌───────────────────────────────────────────────────────────┐
   │ Nivel A: dominio del juego (Screen, Sprite, Layer, ...)   │  ← el dev escribe aquí
   ├───────────────────────────────────────────────────────────┤
   │ Nivel B: composición y máquina (Scene, Band, Scheduler…)   │  ← escape explícito y local
   └───────────────────────────────────────────────────────────┘
```

El nivel B **puede** construir sobre el A (un efecto es una clase de nivel A que por dentro emite
intenciones al plan), pero el nivel A **nunca** depende de que el dev conozca el B.

## 3. Reglas del nivel A

1. **Dominio, no chipset**: los nombres son del juego (`Screen`, `Layer`, `Color`, `Scroll`), nunca
   de la máquina (`Plane`, `BPLCON`, `Blitter`, `FramePlan`, `Rasterizer`, `Scheduler`).
2. **Sin offsets ni punteros en la firma**: nada de `u16*`, `u8*`, strides, número de planos ni
   módulos. Eso viaja **dentro** del asset cocinado y del contexto de dispositivo.
3. **Tipos fuertes en la frontera**: `SpriteId`, `ColorIndex`, `LayerId`, `AssetHandle<T>` son
   tipos distintos; no se puede pasar un índice de color donde va un canal.
4. **Una llamada por intención**, configuración por *designated initializers* con buenos defectos.
5. **Errores sin excepciones**: `[[nodiscard]] bool`/`Result` con motivo; lo de valor no falla.
6. **Sin restricciones**: cualquier capacidad del engine debe poder pedirse desde el nivel A. Si una
   capacidad obliga a bajar al B **siempre**, falta una abstracción de nivel A: se diseña, no se
   limita.

## 4. El escape (nivel B) es explícito y local

- **Puertas nombradas**: `app.device()`/`app.screen()`/`app.scene()`/`app.memory()` para bajar a la
  composición, y desde ahí los tipos de composición (`RasterLayout`, `Band`, `Scheduler`). Bajar es
  una **decisión visible** en el código, no un accidente.
- **Contrato interno documentado**: quien baja asume el contrato del nivel B (alineaciones, orden
  canónico de registros, `MEMF_*`), que está documentado en su doc canónico.
- **Local**: el escape no «contamina» el resto del juego; una función que baja puede volver a nivel
  A en la línea siguiente.
- **Depurabilidad**: como el B es el mismo engine (no un backend oculto), lo que el dev ve al bajar
  es exactamente lo que ejecuta el hardware.

## 5. Ejemplo: el escenario de la demo 213

El caso que motivó este documento —**dual playfield 3+3**, fondo con scroll, un tramo inferior sin
planos para un efecto por raster y BOBs— expresado en los dos niveles:

```cpp
// NIVEL A (objetivo del API): el dev describe el juego, no la máquina.
auto bg = app.world().add_layer({ .image = fondo, .scroll = eng::Scroll::x(2) });
auto fg = app.world().add_layer({ .role = eng::Role::Foreground });     // DPF lo deduce el planner
auto hud = app.world().add_layer({ .role = eng::Role::Overlay, .height = 48 });
app.world().add_effect(eng::effects::RasterGradient { .region = {200, 256}, .colors = cielo });
auto nave = app.assets().sprite("assets/nave.bob");
app.world().add_actor({ .sprite = nave, .at = {x, y} });
app.run();
```

```cpp
// NIVEL B (lo que usa hoy la 213): composición explícita sobre el mismo engine.
eng::scene::RasterLayout layout {};
layout.add(band_of_planes(m_image, kPlanes, kBytesPerRow, 0u));  // banda de fondo
layout.add({.top = 208, .planes = 0, .color = false});           // franja sin planos
layout.materialize(m_sched);
m_bobs.emit(plan, band.bob_target());
```

La tabla de equivalencia (y el estado de cada pieza) está en
[PUBLIC_GAME_API.md](PUBLIC_GAME_API.md): el objetivo es que el nivel A sea **suficiente** para el
caso de la 213 y el nivel B quede como escape, no como camino obligatorio.

## 6. Checklist para juzgar un API (¿es de nivel A?)

- ¿Nombra **hardware** (registro, plano, Blitter, Copper, modo)? → no debe.
- ¿Pide **offsets/planos/strides/módulos** en la firma? → no debe.
- ¿La llamada habitual cabe en **una línea** y se entiende **sin conocer el engine**? → sí.
- ¿Se puede lograr **cualquier** capacidad del engine **sin bajar** al nivel B? → debe (si no, falta
  abstracción).
- ¿Bajar al nivel B es **posible, explícito y documentado** cuando se quiere? → sí.
- ¿El nivel B es **el mismo** motor (no un backend oculto)? → sí.

## 7. Relación con otros documentos

- Principios del API público y frontera público/interno: [PUBLIC_API.md](PUBLIC_API.md).
- Guía de módulo a módulo (qué existe y qué falta, con gates): [PUBLIC_GAME_API.md](PUBLIC_GAME_API.md).
- Deuda de coherencia del API y plan por fases: [ROADMAP_API_COHERENCE.md](ROADMAP_API_COHERENCE.md).
- Modelo interno (algoritmo/superficie/composición/máquina): [PLAYFIELD_SCROLL_ARCHITECTURE.md](PLAYFIELD_SCROLL_ARCHITECTURE.md).
- Estilo y reglas obligatorias de código: [CODING_STYLE.md](CODING_STYLE.md).
