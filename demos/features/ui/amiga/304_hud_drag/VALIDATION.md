# VALIDATION - 304_hud_drag

## Contenido (regla 25)

Fondo del campo con bandas de color estáticas, rejilla de inventario con **iconos reales del atlas** (H6: cuadrado/cruz/círculo/rombo, incbinados con prebuild), **fantasma** del arrastre siguiendo al puntero y **marcas de origen/destino**; el guion determinista completa ciclos de arrastre (drop válido y cancelación alternados) sobre el inventario RPG real.

## Medidas (A500_debug, ventana de 12 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **499** (49,9 fps: 1 campo/frame) | contador 0 |
| `drag_ok` | **2** (ciclos completados: un drop válido y una cancelación) | contador 1 |
| `drag_bad` | **0** | contador 2 |

**Evidencia de memoria** (gate `analyze-sequence.sh`): `fps10 >= 400`, `drag_ok >= 2` y `drag_bad == 0` — la máquina de arrastre completa sus terminales sin fallos, sin depender del vídeo (lección DT-035).

## Visión (prompt canónico; `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Observado: la línea de estado cambia `D:0 SRC:02 DST:--` → `D:2 SRC:00` (fases del arrastre), los iconos del inventario con **contornos limpios** ("sin bandas, halos ni píxeles cortados"), el icono origen resaltado con borde rojo y el diamante con cambios de color (el fantasma/destino). La salida corresponde a la intención.

## Notas

- La captura por paso de este entorno puede congelar el vídeo (DT-035): el gate usa **intervalos** (CPU viva) + memoria.
- El fantasma se pinta en la **pasada de iconos** (después del `apply`), así que el `DragSession` del modelo no pinta nada: es estado, la presentación es del juego.
