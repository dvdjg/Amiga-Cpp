# VALIDATION - 306_hud_crpg

## Contenido (regla 25) y H5/H6

- **Dos regiones simultáneas** (cierre del H5): la banda (verbos + rejilla + tooltip) y un **overlay** en pantalla con dos botones (EQUIP/DROP) pintados sobre el campo; el juego despacha de delante hacia atrás con `hud_hit_test_region` (las entradas llevan `HudHitRect::region`; HOST-469 lo verifica). El overlay se comprueba además por conteo de píxeles en su caja (9.984 px pintados en la captura).
- **Tema con presets y cambio en caliente** (H6): `hud_theme_classic()` ↔ `hud_theme_amber()` cada 300 frames con repintado completo de la banda; el tema es **contrato de la región** (`set_region_theme`/`region_theme`, instantánea por valor en el modelo; HOST-472 verifica). **Ciclo verificado en vivo** (2026-10-19): contador `iconset`=2 en 16,5 s (825 frames) y la secuencia captura el cambio (iconos rojo→blanco, verbos LOOK→TAKE→USE, fps 43,9→45,0).
- **Conjunto de iconos conmutable** (H6): `IconSet` A/B (mapeo tipo→frame del atlas + remapeo de los colores del icono), cambiado con el tema y reconstruyendo las vistas por el puente RPG (`rebuild_slots`); contador 3 (`iconset`) como evidencia de memoria.
- **Atlas de iconos integrado** (H6): `src/prebuild.sh` regenera `out/assets/hud/test_icons` con `tools/assets/hud-atlas.mjs` y la demo incbina el `.bin` (2 planos, interleaved por filas, color 0 = transparente) pintándolo por CPU en una **pasada de iconos** tras el `apply`; la visión lee las **formas** de las celdas (cuadrado, cruz, círculo, rombo — ya no son tiras de color).
- **Espera de VBlank activa** (`run_demo(..., VblankWait::Active)`, familia 212/DT-011): con el bucle por señal la app quedaba **estancada de forma determinista a los ~262 frames** (el SO vivo apagaba el latido; el frame no volvía a avanzar y el ciclo de 300 frames nunca se disparaba). Con la espera activa (IRQ + sondeo de VPOS) la demo corre indefinidamente (825 frames medidos) y el ciclo se dispara.

## Medidas (A500_debug, ventana de 16,5 s, espera activa)

| Métrica | Valor | Fuente |
|---|---|---|
| `fps10` | **439-499** (43,9-49,9 fps; el mínimo coincide con las ventanas que contienen un repintado completo por cambio de tema) | contador 0 |
| `state` | verbo*100 + selección (p. ej. 499 = verbo 4, sin selección) | contador 1 |
| `aframe` | frames de guion (se **reinicia con entrada real**: ratón/joystick); 726 en la ventana larga | contador 2 |
| `iconset` | **2** — cambios de conjunto de iconos (dispara el ciclo de tema cada 300 frames) | contador 3 |
| `hud_ops` | operaciones del modelo aplicadas en el **último frame** (0 en frames sin deltas; sube con los repintados completos por cambio de tema) — dogfooding de `HudStats` (H4, `budget.hpp`) | contador 4 |
| `vbl` / `frame` | **601 / 503** en la última ventana (ratio 0,837: la demo está en el borde de 1↔2 campos; con la espera activa consume ~todo lo que produce) | contadores 5/6 |

## Puntero de ratón (sprite de hardware)

La demo activa el **puntero de GUI** (`display.hardware_cursor = true`): la flecha clásica (cuerpo blanco + contorno negro) sobre el sprite 0, movida con los deltas de `poll_mouse` (`app.cursor()->move_by`). La estructura DMA, la emisión (`SPR0PT`/`SPREN`), la flecha y el controlador están verificados en **HOST-301**; la captura PNG del runner **no incluye sprites** (por eso no aparece en la visión). Medido: la demo queda en su baseline (~41 fps, ratio 0,84); con el enlace de otro layout se midió el salto a 2 campos (~26 fps), así que el margen de esta demo es estrecho (candidato a optimización).

Sin picos medidos con la instrumentación temporal (retirada): el frame repinta solo las celdas/verbos cuyo estado cambió + la línea del tooltip al expirar; el cambio de tema repinta la banda completa cada 300 frames. La pérdida de latidos de la familia DT-036 queda resuelta en esta demo con la espera activa.

## Evidencia de memoria (gate `analyze-sequence.sh`)

- `fps10 >= 400` y `aframe >= 50` tras la ventana: el guion **avanza** (recorre el perímetro de la rejilla, salta a los verbos y activa) — prueba de interacción **sin depender del vídeo** (lección DT-035).
- **Fix de captura (DT-035)**: el modo `--sequence-step-frames` (probe + CPU parada por frame) sirve **vídeo congelado** en este entorno; el gate usa **`--sequence-frames` por intervalos** (CPU viva): cada par de frames difiere (480-4700 px), hay 5 firmas distintas de la línea de estado y el foco (rojo) aparece en todos los frames.

## Visión (prompt canónico, `tools/analyze/vision-prompt.txt`; modelo `qwen3-vl:8b-instruct-q8_0`)

Respuesta cruda en `src/VISION.md`. Resumen de lo observado: barra de verbos `LOOK TAKE USE OPEN TALK` con **TALK resaltado en amarillo**, rejilla de celdas a la izquierda con la celda enfocada marcada (blanco con borde rojo), líneas de estado `V:4 S:--` y `VERB:... ITEM:...`, franja de depuración `HUD CRPG v1.0.NNNN F... FPS 46.9`, fondo azul oscuro sin artefactos ("no hay bandas, halos, píxeles cortados ni restos"); el cambio entre frames descrito = el bloque resaltado del HUD moviéndose (el guion). **Conclusión:** la salida se corresponde con la intención (pantalla CRPG con verbos, inventario y estado) y no muestra defectos.

## Cautelas / pendiente

- El guion se **reinicia con entrada real** (ratón/joystick): en runs con `--warp` el ratón emulado puede emitir deltas y mantener `aframe` bajo; la ventana de memoria se mide **sin warp**.
- La línea `VERB:` del tooltip puede leerse con el verbo anterior si la visión examina una línea no repintada del todo; el estado `V:n` (pintado directo sobre la superficie) es la fuente fiable.
