# VALIDATION — 220_sprite_bend (bending por línea, F4.2)

Informe de validación de la técnica F4.2: dos columnas de 16×64 dobladas por Copper con una tabla de seno, en oposición de fase. Campaña: 2026-10-13, A500_debug (WinUAE-DBG).

## Qué se pretendía

1. Dos columnas (canales 2 y 3) con la X desplazada **por línea** según un seno (onda suave, sin peldaños ni cortes), ondulando en oposición.
2. La onda se mueve entre frames (fases avanzan ~2 px/frame).
3. 50 fps sin tirones; sin artefactos (columnas fantasma, líneas sueltas).

## Medidas

| Métrica | Valor |
|---|---|
| `measure-fps.mjs` (20 s) | **49.97 fps** emulado; 1001 frames; **1.00059** campos/frame |
| `g_eng_frame_telemetry.copper_words` (canal lateral) | **764** words (bending 2×64 líneas = 640 + display/armado) |

## Evidencia de visión (Ollama `qwen3-vl:8b-instruct-q8_0`, 3 capturas a ~0.4 s)

Respuesta cruda resumida:

> 1) **Dos columnas**… de **tubo**, con un **núcleo claro** y un **borde punteado**… 2) **Curvadas/onduladas**… líneas que se desplazan horizontalmente de forma suave, creando la ilusión de una onda… 3) **Sí, la forma cambia**: en el primer frame forman una **V**; en el segundo… una **X**… las columnas se doblan en oposición… 4) **No se observan cortes, saltos bruscos, basura ni líneas sueltas**… animación **suave y coherente**… bordes limpios.

## Conclusión (alcance exacto)

- **VERIFICADO**: onda por línea en los dos canales con emisión intercalada (visión: curvatura suave, oposición, movimiento entre frames, sin artefactos); cadencia 49.97 fps/1.00059 campos; coste de copperlist medido (764 words).
- **NO CUBIERTO**: alturas grandes (>128 líneas se recortan por contrato; el pacing del Copper a 128 líneas/channel no se ha medido); combinación con scroll horizontal del playfield (el offset de fetch podría reducir la ventana del Copper — familia del pacing de la 215).
- **Hallazgo incorporado al engine**: la emisión de bending de varios canales debe ser **intercalada por línea** (un `WAIT` compartido); por bloques separados los `WAIT` del segundo canal quedan fuera de orden (detectado en la primera iteración de esta demo; cubierto por HOST-436).

## Reproducir

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/220_sprite_bend --debug
node tools/debug/measure-fps.mjs 220_sprite_bend A500_debug --json
```
