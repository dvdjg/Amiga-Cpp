# VALIDATION - 054_sprite_allocator

## Contenido (regla 25)

Fondo **estructurado** pintado por CPU al init (suelo verde, horizonte, tres montañas con cumbres claras, sol naranja, árboles; los índices 6/9/2/14 no los escribe ninguna intención) y **movimiento real**: los nueve actores recorren lissajous con x e y sobre su animación de bitmap de 2 frames; la recomposición + re-emisión de copperlist va cada 2 frames.

## Medidas (A500_debug, ventana de 6 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **332** (33,2 fps; 25,0 recomponiendo cada frame) | contador 0 |
| `comp_ok` / `comp_bad` | **101 / 0** | contadores 1/2 |
| `split` | **2304** = 9 HW + 0 degradados | contador 3 |

**Hallazgo:** al moverse por líneas distintas, el `SpriteAllocator` **multiplexa** los 9 sprites en 8 canales (el reparto estático daba 8+1). El invariante correcto es la suma (`sprites_hw + degradados == actores`); `comp_bad = 0` en toda la ventana.

## Visión

`src/VISION.md` (prompt canónico). La evaluación cubre el fondo, las nueve barras animadas con sus pares de color y la ausencia de artefactos; el cambio de contenido (movimiento) es visible entre frames.
