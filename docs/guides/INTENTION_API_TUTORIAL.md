# Tutorial del API 2D por intenciones

Este documento explica cómo construir un juego 2D con el API de alto nivel del engine y, cuando hace falta, dónde está la implementación interna y por qué se separó. Está pensado para alguien que llega nuevo: primero enseña el camino corto y después el mapa para bajar de nivel sin romper el modelo.

## 1. La idea: declarar intenciones, no registros

El juego describe **qué quiere mostrar** y el engine decide **cómo materializarlo** en el Amiga:

```text
Juego
  SpriteDesc + SpriteHint, World/Layer, ScrollSpec, texto
        │
        ▼
Fachada (eng::api)
  App + Game, SpriteDirector, SpriteBank/GameView, Screen, XlimitedScene
        │
        ▼
Resolución (engine)
  SpriteScene / ActorStore / SpriteAllocator / HwSpritePlacement
  XlimitedScene / compositores Copper
        │
        ▼
Hardware
  Copper, Blitter, sprites DMA, bitplanes, Paula
```

Una intención no es un registro: `SpriteHint` no es `SPRxCTL`, `Camera2D::scroll_x` no es `BPLCON1` y `SpriteDesc` no es `SPRxPT`. El juego no debe escribir registros ni punteros de Copper.

## 2. El bucle mínimo

El contrato de juego es `init` / `update` / `render`, con `App` ocultando el backend:

```cpp
struct MyGame {
    void init(auto& app) {
        // alta de contenido y capas
    }
    void update(auto& app) {
        // simulación: mover cámara, actores, input
    }
    void render(auto& app) {
        // dibujo/HUD; el motor ya compone el frame
    }
};
```

`App` aporta `frame()`, `input()`, `audio()`, `tasks()`, `screen()`, `world()`, `present()` y la pila de escenas. La entrada se consume por mensajes del mini-SO o por el agregador de frame; no se sondea el hardware desde la lógica del juego.

## 3. Ruta corta: `SpriteDirector`

`eng::SpriteDirector` es la fachada pública para sprites simples y compuestos. El juego registra contenido y declara actores:

```cpp
using Director = eng::scene::SpriteDirector<
    8u,     // MaxSprites: actores simples vivos
    8u,     // MaxVisuals: Visual simples registrados
    1u,     // MaxComposites
    1u,     // MaxBands
    16u,    // MaxPlacements
    4u>;    // MaxPaletteEvents

Director director;

void init(auto& app) {
    eng::graphics::Visual visual;
    visual.pixels = pixels;   // DATA Chip ya cocinada
    visual.w = 16u;
    visual.h = 16u;
    visual.bitplanes = 2u;
    const eng::scene::VisualId visual_id = director.add_visual(visual);

    eng::scene::SpriteDesc desc {};
    desc.content = eng::scene::SpriteVisualRef::simple(visual_id);
    desc.hint.policy = eng::scene::SpritePolicy::Sprite;
    desc.hint.channel = 0u;   // opcional: canal estable para parcheo
    desc.x = 120;
    desc.y = 100;
    const eng::scene::SpriteId actor = director.add(desc);
    if (!actor.valid()) { /* presupuesto/contenido */ }
}

void update(auto&) {
    director.set_position(actor, x, y); // pantalla, no HSTART
}
```

`emit` compone canales, placements, paletas de banda y el reporte. Si solo cambian posiciones de actores simples, `refresh_positions` evita ordenar y repartir canales de nuevo; el compositor puede parchear POS/CTL en la copperlist.

### Contrato mental de `SpriteDesc`

| Campo | Significado |
|---|---|
| `content` | `VisualId` simple o `CompositeId`; no punteros al asset |
| `hint.policy` | `Auto`/`Sprite`/`Bob`/`Cpu`/`Layer` |
| `hint.attach` | par attached de 15 colores |
| `hint.reuse` | vertical, cadena, por línea, horizontal |
| `hint.channel` | canal preferido (útil para posiciones estables y parcheo) |
| `x`/`y` | coordenadas de pantalla, no del mundo ni registros |
| `sprite_palette` | paleta opcional del actor |
| `sprite_template` | plantilla de franjas avanzada |
| `line_layer` | capa por línea avanzada |

## 4. `SpriteBank`/`SpriteGameView`: slots y DATA

`SpriteBank<MaxSprites>` agrupa los `SpriteConfig` y una vista Chip de DATA que pertenece a la escena; no posee el bloque de memoria. `SpriteGameView` publica el banco en el `SpriteManager` de la escena. Es la capa útil para juegos que quieren control explícito de slots sin tocar el `SpriteManager`:

```cpp
eng::scene::SpriteGameView<8u> view;
view.bind_data(scene.sprites().sprite_data_view());
if (auto cfg = view.config(0u)) {
    cfg->enabled = true;
    cfg->data = scene.sprites().sprite_data_view().subview(0u, 64u);
    cfg->width_words = 1u;
    cfg->height = 16u;
}
view.publish(scene); // solo cuando cambie la estructura/posición de slots
```

`config()` devuelve una vista vacía para un índice fuera de capacidad; no recorta silenciosamente al último slot. `GameView<MaxSprites>` es un alias corto de `SpriteGameView`; si el lector quiere el nombre explícito, use `SpriteGameView`.

## 5. Plantillas: capacidad fija y coste

Todas las plantillas del camino de juego reservan en BSS y no usan heap. Elegir capacidades ajustadas reduce memoria y trabajo de inicialización.

| Plantilla | Parámetros | Qué controla |
|---|---|---|
| `SpriteDirector<S, V, C, B, P, E>` | sprites, visuales, compuestos, bandas, placements, eventos | actores, contenido, bandas, presupuesto de placements/paleta |
| `SpriteBank<N>` / `SpriteGameView<N>` | slots 1..8 | configuración y DATA por canal |
| `SpriteScene<MaxActors>` | actores simples | escena interna simple y sus intenciones |
| `CompositeScene<MaxActors, MaxIntents>` | actores compuestos e intenciones | partes/secuencias compuestas |
| `XlimitedScene<Consts, Map, Profile>` | geometría, mapa, perfil | playfield de scroll/corcóscru |
| `TileScroll<Backend, W, H, Planes, YTravel, Period>` | viewport, planos, recorrido, período | capa de tiras |
| `TextLine<N>` / `StaticString<N>` | capacidad con terminador | HUD/diagnóstico sin heap |
| `FramePlan` y planes Copper | capacidad fija/arena | trabajo por frame |

Regla práctica: no dimensionar `SpriteDirector` al máximo si el juego usa ocho actores. `MaxSprites` es el número de actores simples vivos, no el número de frames de animación.

## 6. Lo que ocurre por debajo

- `SpriteDirector::add` crea una entidad y da de alta el actor en `SpriteScene` o `CompositeScene`.
- `SpriteScene::emit` ordena actores por superficie/`z`, construye `SpriteIntent`, `SpriteAllocator` reparte los ocho canales y se publican `HwSpritePlacement`.
- `SpriteDirector::emit` combina compuestos y simples con un ledger de canales, ordena placements/paletas y calcula `SpriteLoadReport`.
- `XlimitedDualComposer` recibe los placements y emite la copperlist. Si la estructura es estable, registra `MoveSlot` de `SPRxPOS`/`SPRxCTL` y parchea solo esos datos por frame; si cambia la estructura, reemite.
- `XlimitedScene::refresh_sprite_director` actualiza solo posiciones simples; no reordena canales ni vuelve a ejecutar el allocator.
- `SpriteScene`/`CompositeScene` son **implementación avanzada**: no se incluyen desde `api.hpp` y no son la elección de un juego nuevo.

## 7. Niveles de acceso

**Nivel A — juego normal:** `App`, `Screen`, `World`, `SpriteDirector`, `SpriteBank`/`GameView`, `TextLine`, `HostHud`.

**Nivel B — control de escena:** `XlimitedScene`, `XlimitedScrollLayer`, `ScenePlan`, `ScrollPlan`, `Camera2D`, `SpriteBudget`, `FramePlan`.

**Nivel C — técnicas avanzadas:** `SpriteScene`, `CompositeScene`, `SpriteManager`, `HwSpritePlacement`, `SpriteDmaChain`, `SpriteLineLayer`, compositores y Copper.

Bajar de nivel es aceptable cuando una abstracción falta o cuando se mide la necesidad; no es el camino por defecto. Si una demo escribe registros custom desde la lógica de juego, probablemente falta una API.

## 8. Coste y trampas

- `emit` no es gratis: ordena y reparte canales. Llamarlo solo cuando cambia la estructura o cuando se necesita un reporte nuevo.
- `refresh_positions` es para escenas simples estables; si hay compuestos, `attached`, plantillas o BOB, devuelve fallo y se usa `emit`.
- El parcheo POS/CTL del compositor dual escribe POS/CTL; si cambia DATA, ancho, canal o estructura, se reemite.
- El compositor single tiene parcheo solo cuando no hay split/HUD/zonas de color; el resto hace fallback a emisión completa.
- `SpriteBank::publish`/`invalidate` no debe llamarse cada frame si solo cambian posiciones; el camino correcto es el refresh del director.
- `Screen::target()`/`Screen::blit` son rutas de transición para código avanzado; no forman parte del vocabulario ideal de un juego nuevo.
- No se asigna memoria en gameplay; las capacidades se eligen en compilación.

## 9. Referencias

- `docs/engine/architecture/SPRITE_DIRECTOR.md`: contrato completo del director.
- `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`: ventanas de canales y técnicas avanzadas.
- `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md`: scroll y composición.
- `docs/engine/architecture/SCENE_COMPOSITION.md`: composición de Copper.
- `games/500_caverna`: ejemplo jugable de ocho actores, DPF y HUD.
- `demos/techniques/amiga/sprites/225_sprite_director_stress`: estrés del director.
- `docs/engine/architecture/PUBLIC_GAME_API.md`: estado y evolución de la fachada.
