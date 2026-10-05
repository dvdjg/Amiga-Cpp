# Tutorial 02 — Bucle, VBlank y frame

Por qué el engine separa `update` de `render` y por qué el VBlank importa. Es el concepto que hace que
las cosas «no tiren» en un Amiga.

## El contrato

```cpp
struct MiJuego {
	void init(eng::App& app);    // una vez: recursos + escena
	void update(eng::App& app);  // por frame: lógica (no dibuja)
	void render(eng::App& app);  // por frame: dibujo + app.present()
};
```

El `App` los llama **una vez por VBlank** (50 Hz): `update` **después** del latido, `render` después de
`update`. `app.frame()` da el índice (0, 1, 2, …).

## Por qué el VBlank manda

El Amiga no tiene framebuffer doble por hardware: el display lee los bitplanes **mientras** el DMA
avanza. Cambiar lo que se ve **a media pantalla** parte la imagen. Por eso el engine te da un punto
seguro (el VBlank) para **publicar**:

```
  línea 0 ─────────────────────────────────────────► Agnes lee bitplanes
        │            (el haz pinta)
        │
   VBlank ────────────────────────────────────────► aquí: update → render → present
        │                                            (el haz NO pinta)
        ▼
  línea 312 ──────────────────────────────────────► empieza el frame siguiente
```

Publicar **fuera** del VBlank (a media pantalla) reinicia el Copper y aparece *tearing*. El `App` lo
hace bien: `present()` publica tras el latido.

## `update` vs `render`

| | `update` | `render` |
|---|---|---|
| Qué va | lógica, cámara, actores, física | dibujo (`screen`, sprites), `present` |
| Toca registros | **no** | no (lo hace el engine) |
| Coste de no caber | el juego «va lento» (se salta un frame) | *tearing* si se publica mal |

Regla: **en `update` mueves el mundo; en `render` lo pintas**. El engine conduce sus capas (scroll)
entre ambos (`pump_scroll_layers`).

## Estados de juego: la pila de escenas

Un juego con estados (título → juego → game over) apila **escenas** sobre el `Game`. Mientras hay una
escena arriba, su `update`/`render` **sustituyen** a los del `Game`; `enter`/`exit` son opcionales:

```cpp
struct Titulo {
	void enter(eng::App& app) { app.play_music("title"); }
	void update(eng::App& app) { if (app.input().pad0.fire) app.set_scene(m_juego); }
	void render(eng::App& app) { app.screen().text(8, 8, "PULSA FUEGO", 1); app.present(); }
};
```

Sin heap ni vtable: capacidad fija y despacho por punteros a función (`eng/api/game.hpp`,
`tests/host/ui/240_app_scenes`).

## Cuándo NO hay VBlank

Algunos efectos (o un emulador) quieren **producir el frame** y solo pedir que se muestre. El engine lo
permite: usa `present_indices`/C2P y tu propia sincronía, o corre tu lógica en un callback por frame.
El bucle por defecto ya es «una iteración = un frame».

## Siguiente

Tutorial 03 — *Dibujo con `Screen`* (planificado; ver la tabla de [Tutoriales](README.md)).
Arquitectura del frame: [05_arquitectura/frame_path](../05_arquitectura/frame_path.md).

Volver a [Tutoriales](README.md) · [índice del manual](../README.md).
