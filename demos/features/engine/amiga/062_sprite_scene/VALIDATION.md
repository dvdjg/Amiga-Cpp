# VALIDATION - 062_sprite_scene

## Contenido (regla 25)

Fondo **estructurado** pintado por CPU al init (cielo navy, suelo verde, horizonte, tres montañas con cumbres claras, sol naranja y sin colisión con el copper: los índices 6/9/2/14 no los escribe ninguna intención) y **movimiento real**: los nueve actores recorren lissajous con x e y (recomposición cada 4 frames; la copperlist se re-emite porque las posiciones de sprite viajan en ella).

## Medidas (A500_debug, ventana de 8 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **407** (40,7 fps) | contador 0 |
| `comp_ok` / `comp_bad` | **81 / 0** | contadores 1/2 |
| `split` | **2304** = 9 HW + 0 degradados | contador 3 |

**Hallazgo de la mejora:** con los actores **estáticos** el noveno no cabe (8 HW + 1 degradado, el invariante del demo original); al moverse por líneas distintas el `SpriteAllocator` los **multiplexa** (9 HW + 0 degradados). El invariante correcto es la suma (`sprites_hw + degradados == actores`), no el reparto — `comp_bad = 0` en toda la ventana.

## Visión (prompt canónico; `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Observado: cielo azul oscuro, suelo verde y tres montañas; los nueve bloques de colores con **contornos limpios, sin superposición ni artefactos**, desplazándose entre frames (lissajous). La salida corresponde a la intención (fachada de sprites sobre fondo estructurado, con reparto publicado).

## Notas

- La composición+copper se hacen cada 4 frames (no cada frame): el `install_copper_list` espera la ventana de VBlank y con el bucle de polling duplicaría la espera (medido: 24,9 fps cada frame; 40,7 fps cada 4). El movimiento sigue siendo visible (≈12 pasos/s).
- La captura por paso de este entorno puede congelar el vídeo (DT-035): la secuencia de visión usa captura por intervalos.
