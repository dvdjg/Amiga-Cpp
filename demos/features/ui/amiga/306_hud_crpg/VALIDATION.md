# VALIDATION - 306_hud_crpg

## Contenido (regla 25) y H5/H6

- **Dos regiones simultáneas** (cierre del H5): la banda (verbos + rejilla + tooltip) y un **overlay** en pantalla con dos botones (EQUIP/DROP) pintados sobre el campo; el juego despacha de delante hacia atrás con `hud_hit_test_region` (las entradas llevan `HudHitRect::region`; HOST-469 lo verifica). El overlay se comprueba además por conteo de píxeles en su caja (9.984 px pintados en la captura).
- **Tema con presets y cambio en caliente** (H6): `hud_theme_classic()` ↔ `hud_theme_amber()` cada 300 frames con repintado completo de la banda; el tema es **contrato de la región** (`set_region_theme`/`region_theme`, instantánea por valor en el modelo; HOST-472 verifica).
- **Atlas de iconos integrado** (H6): `src/prebuild.sh` regenera `out/assets/hud/test_icons` con `tools/assets/hud-atlas.mjs` y la demo incbina el `.bin` (2 planos, interleaved por filas, color 0 = transparente) pintándolo por CPU en una **pasada de iconos** tras el `apply`; la visión lee las **formas** de las celdas (cuadrado, cruz, círculo, rombo — ya no son tiras de color).

## Medidas (A500_debug, ventana de 8 s)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **499** (49,9 fps) | contador 0 |
| `state` | verbo*100 + selección (p. ej. 499 = verbo 4, sin selección) | contador 1 |
| `aframe` | frames de guion (se **reinicia con entrada real**: ratón/joystick) | contador 2 |

Sin picos medidos con la instrumentación temporal (retirada): el frame repinta solo las celdas/verbos cuyo estado cambió + la línea del tooltip al expirar. La cadencia queda por debajo de los 49,9 de la 302 (misma familia que DT-036: pérdida de latidos fuera del coste del demo); **SIN VERIFICAR** el desglose por sección (DT-036 sigue abierta).

## Evidencia de memoria (gate `analyze-sequence.sh`)

- `fps10 >= 400` y `aframe >= 50` tras la ventana: el guion **avanza** (recorre el perímetro de la rejilla, salta a los verbos y activa) — prueba de interacción **sin depender del vídeo** (lección DT-035).
- **Fix de captura (DT-035)**: el modo `--sequence-step-frames` (probe + CPU parada por frame) sirve **vídeo congelado** en este entorno; el gate usa **`--sequence-frames` por intervalos** (CPU viva): cada par de frames difiere (480-4700 px), hay 5 firmas distintas de la línea de estado y el foco (rojo) aparece en todos los frames.

## Visión (prompt canónico, `tools/analyze/vision-prompt.txt`; modelo `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Resumen de lo observado: barra de verbos `LOOK TAKE USE OPEN TALK` con **TALK resaltado en amarillo**, rejilla de celdas a la izquierda con la celda enfocada marcada (blanco con borde rojo), líneas de estado `V:4 S:--` y `VERB:... ITEM:...`, franja de depuración `HUD CRPG v1.0.NNNN F... FPS 46.9`, fondo azul oscuro sin artefactos ("no hay bandas, halos, píxeles cortados ni restos"); el cambio entre frames descrito = el bloque resaltado del HUD moviéndose (el guion). **Conclusión:** la salida se corresponde con la intención (pantalla CRPG con verbos, inventario y estado) y no muestra defectos.

## Cautelas / pendiente

- El guion se **reinicia con entrada real** (ratón/joystick): en runs con `--warp` el ratón emulado puede emitir deltas y mantener `aframe` bajo; la ventana de memoria se mide **sin warp**.
- La línea `VERB:` del tooltip puede leerse con el verbo anterior si la visión examina una línea no repintada del todo; el estado `V:n` (pintado directo sobre la superficie) es la fuente fiable.
