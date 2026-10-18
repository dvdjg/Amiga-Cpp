# VALIDATION - 306_hud_crpg

## Medidas (A500_debug, ventana de 8 s tras READY)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **456** (45,6 fps; 499 = 49,9 en modo warp) | contador 0 del periférico |
| `state` | verbo*100 + selección (p. ej. 401 = verbo 4, celda 1) | contador 1 |
| `aframe` | **276-277** frames de guion en 8 s (el anillo recorre y activa) | contador 2 |

Sin picos medidos con la instrumentación temporal (retirada): el frame repinta solo las celdas/verbos cuyo estado cambió + la línea del tooltip al expirar. La cadencia queda por debajo de los 49,9 de la 302 (misma familia que DT-036: pérdida de latidos fuera del coste del demo); **SIN VERIFICAR** el desglose por sección (DT-036 sigue abierta).

## Evidencia de memoria (gate `analyze-sequence.sh`)

- `fps10 >= 400` y `aframe >= 50` tras la ventana: el guion **avanza** (recorre el perímetro de la rejilla, salta a los verbos y activa) — prueba de interacción **sin depender del vídeo** (lección DT-035).
- **Fix de captura (DT-035)**: el modo `--sequence-step-frames` (probe + CPU parada por frame) sirve **vídeo congelado** en este entorno; el gate usa **`--sequence-frames` por intervalos** (CPU viva): cada par de frames difiere (480-4700 px), hay 5 firmas distintas de la línea de estado y el foco (rojo) aparece en todos los frames.

## Visión (prompt canónico, `tools/analyze/vision-prompt.txt`; modelo `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Resumen de lo observado: barra de verbos `LOOK TAKE USE OPEN TALK` con **TALK resaltado en amarillo**, rejilla de celdas a la izquierda con la celda enfocada marcada (blanco con borde rojo), líneas de estado `V:4 S:--` y `VERB:... ITEM:...`, franja de depuración `HUD CRPG v1.0.NNNN F... FPS 46.9`, fondo azul oscuro sin artefactos ("no hay bandas, halos, píxeles cortados ni restos"); el cambio entre frames descrito = el bloque resaltado del HUD moviéndose (el guion). **Conclusión:** la salida se corresponde con la intención (pantalla CRPG con verbos, inventario y estado) y no muestra defectos.

## Cautelas / pendiente

- El guion se **reinicia con entrada real** (ratón/joystick): en runs con `--warp` el ratón emulado puede emitir deltas y mantener `aframe` bajo; la ventana de memoria se mide **sin warp**.
- La línea `VERB:` del tooltip puede leerse con el verbo anterior si la visión examina una línea no repintada del todo; el estado `V:n` (pintado directo sobre la superficie) es la fuente fiable.
