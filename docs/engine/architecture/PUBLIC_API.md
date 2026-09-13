# API pública del engine (principios)

Reglas de diseño de la **interfaz que consume el desarrollador de juego/demo**. El modelo interno
(algoritmo/superficie/composición/máquina) está en `PLAYFIELD_SCROLL_ARCHITECTURE.md`; aquí se fija
cómo se **oculta** al llamador.

## 1. Objetivo

La aplicación se escribe **sin ningún concepto de hardware**: no conoce bitplanes, planos,
`BPLCON`, Copper, Blitter, registros, punteros ni modos de vídeo. Describe **qué quiere ver**
(capas, cámaras, sprites, efectos) y el engine decide **cómo** materializarlo en el chipset.

## 2. Frontera público / interno

- Público: `engine/include/eng/api/` (y tipos de valor de `eng/core/`). Interno: el resto de
  `engine/include/eng/**`, `engine/src/platform/**`.
- En el código de la aplicación **está prohibido**: incluir headers de hardware
  (`<hardware/*.h>`), nombrar registros (`BPL1MOD`, `DMACON`, `COLOR00`…), manejar planos o
  punteros de bitplane, llamar a `hardware_view()`, o elegir explícitamente un modo de display
  (single/DPF/HAM/chunky). Esas decisiones son del planner interno.
- El backend y la composición son los **únicos** que ven el hardware.

## 3. Modelo de alto nivel

```cpp
eng::AppConfig cfg { .arena = eng::chip_ram(512_KiB), .screen = eng::Screen::pal(320, 256) };
eng::App app { cfg };

auto& world = app.world();
// Capas: el dev describe contenido y rol; el planner elige la composición.
auto bg  = world.add_layer({ .tiles = "levels/forest.tmx", .scroll = eng::Scroll::xy(2, 1) });
auto fg  = world.add_layer({ .tiles = "levels/towers.tiles", .role = eng::Role::Foreground });
auto hud = world.add_layer({ .text = score, .role = eng::Role::Overlay, .height = 48 });
// Efectos reutilizables, no código que toca registros.
world.add_effect(eng::effects::RasterGradient { .region = {200, 256}, .colors = sky });
world.add_effect(eng::effects::CopperChunky { .region = {224, 256}, .source = plasma });

world.add_actor({ .sprite = hero, .at = { x, y } });
app.run();
```

- **El dev no elige DPF/HAM/chunky**: añade capas/efectos y el planner deduce la composición.
- Cámaras y mundo: el dev trabaja en **coordenadas de mundo**; el engine traduce a pantalla.
- Los recursos (tiles, paletas, sprites, audio) son **handles** a assets cocinados (UAF-R), no
  punteros.

## 4. Planner (compilación de escena)

Entre la descripción de la app y el frame hay un **planner** (`RenderCompiler`) que:

- resuelve la **composición** (single / EHB / DPF / soft-DPF / HAM / tramos con `ModeSwitchZone` y
  capas efectistas) a partir de las capas/efectos;
- **falla rápido en init** si la escena no cabe en el hardware (planos, DMA, Chip RAM, ancho de
  fetch), con un diagnóstico legible — nunca degrada en silencio;
- produce el **plan por frame** (buffers, splits, presupuesto de Blitter, lista de Copper), de
  forma determinista.

El planner es el único componente que traduce "capas/efectos" a "modo de vídeo + registros".

## 5. Seguridad de tipos y errores

- **Tipos fuertes** en la API (`eng::ColorIndex`, `eng::LayerId`, `eng::Pixels`, `eng::TileCoord`),
  no enteros sueltos; evita mezclar color/plano/coordenada.
- **Handles** con generación para recursos vivos; nunca punteros crudos ni offsets.
- **Vistas** (`eng::Span`/`eng::mdspan`) para buffers de assets; el tamaño viaja con la vista.
- **Errores sin excepciones**: `eng::Result<T>`/`std::expected` y `[[nodiscard]]` en toda operación
  que pueda fallar; los errores nombran la causa (no "false").
- **Dos fases explícitas**: `init` (asigna y valida) y `frame` (no asigna, no falla si la escena
  fue válida). El tipo lo hace imposible de mezclar.

## 6. Presupuesto y determinismo

- El dev puede dar **pistas** de presupuesto en términos abstractos (p. ej. "este efecto es caro,
  tolero bajar a 25 fps"), nunca en registros ni ciclos. El engine lo aplica y lo mide.
- Sin heap en `frame`; matemática **fixed-point** determinista (reproducible/testable).
- La API expone **telemetría** (fps, carga, motivos de caída) como datos, no como registros.

## 7. Extensibilidad

- Los **efectos** son conceptos (template/concept) con parámetros, no herencia virtual: un dev puede
  añadir un efecto propio sin tocar el engine.
- Configs con **designated initializers** y campos añadibles sin romper llamadas existentes.
- Ninguna funcionalidad nueva debe obligar a la app a conocer el hardware: si lo necesita, es que
  falta una abstracción.

## 8. Fugas actuales a eliminar

Las demos consultan directamente la superficie y el display (`hardware_view()`, `mapposx()`,
`display_offset()`, `make_bg_plane_copy_*`). En el modelo objetivo eso es **interno**:

- la app usa una **cámara** (`world.camera(layer)`) para posicionar/leer el scroll;
- el acceso a la vista hardware queda **solo** para la composición/debug (`eng::debug::`), no para
  la lógica.

## 9. Herramientas C++23 que se aprovechan

- **concepts** para policies (`ScrollStrategy`, `Effect`, `Sink`) y para el `Game`.
- **`std::expected`/`Result`**, `[[nodiscard]]`, `enum class` + `std::to_underlying`.
- **`std::span`/`std::mdspan`** (o `eng::Span`) para vistas de assets; **`<bit>`** para máscaras.
- **`constexpr`/`consteval`** para validar configuración en compile-time cuando es posible.
- **Designated initializers** y NTTP para geometría (sin `__udivsi3`).
- **`[[likely]]`/`[[unlikely]]`** en ramas del frame.
- **Sin** STL hosted, `std::function`, contenedores dinámicos ni virtuals en el hot path.

## 10. Qué NO debe aparecer nunca en la app

`Bitplane`, `Playfield`, `BPLCON*`, `Copper`/`CopperList`, `Blitter`, `DMACON`, `planes`, `split`,
`DPF`/`HAM`/`chunky` como flags, punteros, offsets de memoria, ni la elección de modo de display.

## 11. Referencias

- Modelo interno: `PLAYFIELD_SCROLL_ARCHITECTURE.md`.
- Efectos (intenciones): `VISUAL_EFFECT_SPRITE_DESIGN.md`.
- Estilo y restricciones: `CODING_STYLE.md`.
- Assets cocinados: `docs/tools/UAF_PACK.md`.
