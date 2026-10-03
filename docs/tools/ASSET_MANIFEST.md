# Manifiesto de assets (`assets.manifest.json` → `assets.manifest.hpp`)

Fuente única de los assets de una demo/juego. Un **JSON** declara, por asset, su **nombre de
dominio**, su **tipo** y su **geometría**; un **generador** emite el header C++ que consume el
código de juego. Así **la geometría (dimensiones, planos, frames) deja de ser dato del juego**: vive
en el JSON, no en `main.cpp`. Ver `ROADMAP_GAME_API.md` §4.

```
assets.manifest.json   (fuente: nombre, tipo, ruta, geometría)
        │  tools/assets/gen-manifest.mjs
        ▼
assets.manifest.hpp    (generado: INCBIN + accesores + register_assets)
        │
        ▼
Game::init:  if (!ns::register_assets(m_assets)) { falla }
             m_sprite = m_assets.sprite("bob");   // sin geometría
             (void)app.audio().play_music(m_assets.music("mod"));
```

## Formato del JSON

```jsonc
{
  "namespace": "abyss",              // prefijo de los símbolos y namespace de los accesores
  "include": "support/gcc8_c_support.h",  // opcional (por defecto ese): donde vive `INCBIN`
  "assets": [
    { "name": "img", "kind": "bitmap",  "path": "assets/amiga/sprites/abyss/abyss.bpl",
      "width": 320, "height": 256, "planes": 5, "layout": "interleaved" },
    { "name": "bob", "kind": "sprite",  "path": "assets/amiga/sprites/abyss/bob.bpl",
      "width": 32, "height": 16, "planes": 5, "frames": 6, "frame_stride": 640,
      "layout": "interleaved", "draw": "cookie_cut", "mask_pack": "interleaved_pair" },
    { "name": "mod", "kind": "music",   "path": "assets/amiga/audio/testmod.p61" },
    { "name": "pal", "kind": "palette", "path": "assets/amiga/sprites/abyss/abyss.pal",
      "colors": 32 }
  ]
}
```

- **`kind`**: `bitmap` (imagen planar), `sprite` (hoja de BOB), `music` (módulo) o `palette`
  (palabras COLOR). Cada tipo exige su geometría y se valida contra el tamaño real del blob.
- **`path`**: ruta **relativa a la raíz del repo** (la que usa `INCBIN` en el build).
- **`bytes`** (opcional): tamaño esperado del fichero; el generador falla si no cuadra. Para
  `bitmap`/`palette` el tamaño se deduce de la geometría; `sprite` valida `frames × frame_stride`.

## El header generado

Por cada asset, un accesor (`<name>_data()`/`<name>_size()`; `<name>_words()` para paletas;
`<name>_desc()` con la geometría del sprite) y, además:

- `namespace <ns>::register_assets(Assets&)` — registra **todos** los assets en el `Assets` del
  juego **con su geometría** en una llamada. Es plantilla (`template <class Assets>`) para no
  acoplar el header a `eng/api/assets.hpp`: lo incluye el juego. `bitmap` usa `add_bitmap`, `sprite`
  usa `add_sprite` (el engine **guarda** la geometría, y `m_assets.sprite("bob")` la recupera sin
  volver a pasarla).

## Generación y gate

```
# Regenerar el header desde el JSON (tras editar el JSON):
node tools/assets/gen-manifest.mjs demos/.../src/assets.manifest.json

# Comprobar que el header commiteado está sincronizado (gate; también corre en la regresión):
node tools/assets/gen-manifest.mjs demos/.../src/assets.manifest.json --check
node tools/check/asset-manifests.mjs      # todos los manifiestos de demos/ y games/
```

El header es **generado**: no se edita a mano (el gate `asset-manifests` falla si diverge del JSON).

## Relación con UAF-R

Este manifiesto resuelve el **borde C++ ↔ datos** (nombres, geometría y registro). El formato
**binario cocinado** de los assets (UAF-R) y su *packer* son otra pieza: `docs/tools/UAF_PACK.md`.
Un `register_assets` puede cargar desde UAF-R igual que desde un `.bpl`, porque consume bytes.
