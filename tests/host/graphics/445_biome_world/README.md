# HOST-445 — biome_world

Test host del **sistema de biomas por máscaras** (`eng/graphics/proc/terrain_biome.hpp`) y
del **ruido entero** que lo alimenta (`eng/graphics/proc/noise_tile.hpp`): la evolución del
colocador por catálogo a un generador con **conocimiento explícito de compatibilidad**.

## Qué cubre

- **Ruido entero (`mulu.w`)**: nodo hash determinista, `div255` exacto, `smoothstep_q8`
  monótona (tabla `constexpr`), `tile_noise2` **tileable** (`period << cell_log2`) y
  `tile_fbm2` con rango amplio — sin `__mulsi3` ni divisores (40-70 ciclos/muestra en
  68000 frente a ≈19,7 k del ruido escalar).
- **Gramática de adyacencia**: el agua solo toca arena/hierba; tierra y roca junto al agua
  están prohibidas y el generador las repara con playa.
- **Set por máscaras** (`BiomeTileSet::tile_for`): la misma máscara devuelve la misma
  pieza; una máscara girada reutiliza el pintado con variante (≤ 3 piezas nuevas por
  máscara); **las 256 máscaras de campo** (4 lados × 4 materiales) son representables y la
  pieza devuelta casa con la máscara pedida (comprobado por rotación).
- **Generador de mundo** (`generate_biome_world`, 20×14 de celdas de 16×16): conectado
  **por construcción** (`biome_world_connected` sobre las máscaras instanciadas), gramática
  respetada, determinista (dos corridas idénticas), todo en paleta, con castillo, puente,
  casas, cueva y ≥ 40 máscaras distintas.
- **Sonda `constexpr`**: con un atlas plano de juguete, el mundo completo se genera en
  **compilación** (`static_assert`) — la demo 119 evalúa además el atlas real con
  `-fconstexpr-ops-limit` (ver su `build.args`).
- **Dos escalares** (negativo de §1.11): `MiniFloat16` (el válido en Amiga) y `float`.

`--dump <dir>` escribe `biome_map.ppm` (320×224) para la evidencia visual con el modelo de
visión local (Ollama): es el MISMO mundo que incrusta la demo 119.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/445_biome_world
```
