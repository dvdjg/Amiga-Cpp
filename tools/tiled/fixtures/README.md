# Fixtures del parser Tiled

Fixtures pequeños y deterministas para T0/T1. No añaden binarios: las imágenes apuntan al PNG de prueba ya versionado `assets/amiga/hud/test_icons.png` (32×32, cuatro celdas de 16×16), que el parser solo referencia, no decodifica.

| Fichero | Qué cubre |
|---|---|
| `ortho.tmx` + `ortho.tsx` | Ortogonal finito, atlas, TSX externo, grupo con opacidad/paralaje, capa oculta, capa de imagen, objectgroup con spawn/polígono/tile object/punto/texto, propiedades tipadas y de clase, flips H. |
| `collection.tsx` | Tileset de colección (`columns="0"`) con IDs locales con hueco, imagen por tile y animación. |
| `infinite.tmx` | Mapa infinito con chunks negativos y GID con flip H; origen lógico conservado. |
| `iso.tmx` | Orientación isométrica con `tilewidth`/`tileheight` de rejilla; solo IR, el render iso llega en T7. |

Los fixtures se usan desde `tools/tiled/test-parse-tmx.mjs`. Para añadir uno nuevo, mantén el XML mínimo, sin imágenes nuevas y sin depender de una versión concreta del editor más allá de TMX 1.10/1.11.
