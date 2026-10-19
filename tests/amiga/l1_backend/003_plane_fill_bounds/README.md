# L1-003: relleno por plano del Blitter con caras en el borde

Test **on-target** del camino de contorno + `area fill` por bitplane con polígonos que llegan exactamente al borde del bitmap. Reproduce la clase de fallo del residuo del borde derecho de la demo 088 y la cierra con una comparación contra la referencia CPU.

## Qué valida

El trazador de líneas del Blitter escribe cada píxel **un paso por detrás** de la posición que lee (el primer píxel va por el canal D y el resto por C, que ya avanzó). Consecuencia: una vertical dada en `x` se dibuja en `x-1` y sus filas, un paso más abajo. Un vértice en `x == width` (un píxel **fuera**, como deja el clip de cámara a ±1 px del viewport) deja el contorno en la palabra siguiente de la fila; uno en `x == 0` se dibuja en `x == -1` y, por el complemento a dos, cae en la **cola de la fila anterior** (`x≈width-1`). El contorno queda fuera del bbox de la cara y el clear por rectángulo sucio no lo borra nunca: residuo permanente.

Por eso el relleno por plano recorta cada cara al **rectángulo seguro del contorno** antes de planificar las aristas (`plan_plane_edges_in_bounds`): `min_x = 1` absorbe el desfase del trazador y `max_x/max_y = width-1/height-1` son el último píxel incluido. Así **ninguna línea sale del bitmap** y el contorno queda **cerrado** en el borde, que es lo que el `FILL_XOR` necesita para la paridad par dentro de la caja.

La sonda es **estática y determinista**: una sola postura, cinco caras en bandas separadas y una pasada del Blitter:

| Caso | Geometría | Contrato |
|---|---|---|
| control | (40,20)-(120,60) | interior: no toca bordes |
| borde derecho | (280,20)-(320,70) | `x == width` (320) se recorta y el contorno queda dentro |
| borde izquierdo | (-1,90)-(60,130) | `x < 0` se recorta con margen de 1 px (el trazador dibuja en `x-1`) |
| borde inferior | (100,200)-(180,232) | `y == height` (232) se recorta al último píxel incluido |
| fuera | (400,20)-(500,60) | no debe escribir nada |

El mismo conjunto se pinta con la referencia CPU (`fill_polygons_by_plane_cpu`, oráculo portátil validado por HOST-217/474/482) y se publica el veredicto en `g_eng_run_status.detail`:

| Bit | Comprueba |
|---|---|
| 0 | no hay píxeles fuera de las cajas esperadas dilatadas 1 px (el residuo del borde cae lejos) |
| 1 | filas 232..255 limpias antes del HUD (nada escrito fuera del mundo) |
| 2 | columnas del borde (x=0 y x=319) limpias (margen seguro del contorno) |
| 3 | interiores de los cuatro casos rellenos |
| 4 | nada perdido respecto a la CPU (±1 px por el desfase del trazador) |
| 5 | fila 0 limpia — **informativo**: falla por la escritura fantasma del primer píxel de cada línea por el canal D (base del plano), defecto conocido y aparte |

`verify.sh` exige `detail & 0x1f == 0x1f` e informa del bit 5.

## Ejecución

```bash
bash tests/amiga/l1_backend/003_plane_fill_bounds/verify.sh
```

Salida esperada:

```
[L1-003] OK: relleno por plano con caras en x=320, x=-1 e y=232, sin residuo en el borde.
[L1-003] aviso: fila 0 con la escritura fantasma del canal D (defecto conocido, ver README).
```

## Evidencia del A/B

- Con el margen seguro (`min_x = 1`): `detail & 0x1f == 0x1f` → OK.
- Volviendo a `min_x = 0` (comportamiento anterior): `detail == 2` (solo el bit 1) → FAIL. La captura muestra la vertical de la cara izquierda pintada en `x≈width-1` de la fila anterior, exactamente el residuo de la 088.

## Inspección paso a paso

El test admite `DEMO_FLAGS=-DPROBE_CONTOURS_ONLY` (en `build.args`): dibuja **solo los contornos** con el mismo planificador y la misma llamada de línea que el relleno. Es la forma de ver en el framebuffer qué traza el Blitter antes del `area fill`: con el margen seguro, la cara izquierda traza en x=0 y x=59 (no en x=319) y la derecha en x=279 y x=318.
