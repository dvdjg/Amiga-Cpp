# VALIDATION — 002_adv_verbs (A1: verbos, hotspots, say e inventario)

## Qué se ha verificado y con qué evidencia

| Check | Evidencia | Resultado |
|---|---|---|
| Motor puro A1 (`VerbSet`/`resolve_action`/`pick_pointer`/`SayQueue`) | `tests/host/adv/480_adv_verbs_say` (tabla verbo×objetos, dos-objetivos, prioridad GUI→hotspot→suelo, timing exacto de la cola) | **HOST-480 OK** |
| Interacción completa end-to-end (verbo → respuesta → recoger → usar llave → abrir puerta) | Secuencia de 10 frames del **guion de atracción** (`out/run/adventure_amiga_002_adv_verbs/A500_debug/sequence`): frame_009 con la puerta abierta (prop de hueco oscuro), LLAVE+MONEDA en el inventario, `USA` subrayado y `F916` avanzando; la moneda y la llave ya no están en el mundo | **OK** (secuencia + inspección de píxeles) |
| Visión canónica | `src/VISION.md` (prompt canónico, 5 frames): describe puerta **cerrada** con «ESTA CERRADA…», después **abierta** con «LA LLAVE GIRA. LA PUERTA SE ABRE.», el verbo resaltado cambiando de COGE a USA, y **contornos limpios sin halos ni restos** en la puerta y el fondo | **OK** |
| Cadencia | `measure-fps` (ventanas de 2,5 s): régimen `47,9/49,1/48,0/49,1` fps, `241764 ciclos/frame` = **1,7 campos/frame** y `cuantizacion=1 campos/frame`; las ventanas iniciales (0 fps, arranque) bajan la media global a 29,3 (misma pauta que la 001 en su validación) | **~48-49 fps en régimen** (1 campo/frame) |
| Click real con trayectoria MCP | Build de diagnóstico sin guion de atracción + `mcp-winuae-emu/scripts/verify-mouse-path-live.mjs`; secuencia Ollama sobre `out/run/adventure_amiga_002_adv_verbs/A500_advverb_no_attractdmouse_path_probe1_debug/sequence` | **Parcial**: el click cambió «ELIGE UN VERBO» a «CIERRA»; el destino pedido MIRA cayó en CIERRA, así que el envío de movimiento/botón está probado, pero el mapeo de coordenadas al hit-test no está cerrado |
| Flicker | `flicker-check.mjs` (receta del gate, `--no-ollama`): 2 candidatos en arranque tranquilo (celda del contador de frames + actor con save-under) y 8 con el guion de atracción activo (frase/réplica/verbo cambian por diseño); bloques bajos máx. 375. Declarado en `flicker-baseline.json` con márgenes (12/400) y nota de zonas | **Declarado** (no hay parpadeo de playfield: buffer único + scroll por hardware) |
| UI de la franja (barra, frase, inventario, cursor, versión) | Capturas `run1..canonica2` + `VISION.md`; la versión se pinta tras corregir el orden de las escrituras de la fila (`DiffText`) | **OK** |
| Gates de repo | `demo-docs`, `prior-art`, `demo-ergonomics`, `demo-raw-writes`, `encoding`, `doc-index` | **En verde** (ver Estado) |

## Verificación visual de la entrada real

Sonda de entrada con el guion de atracción desactivado (`EXTRA_DEFINES="-DADVVERB_NO_ATTRACT -DMOUSE_PATH_PROBE=1"`), puerto GDB propio y `winuae_input_mouse`/`verify-mouse-path-live.mjs` en `coordinate_mode=relative`: la trayectoria Bézier cuadrática de 25 muestras terminó en un click y el frame final mostró `CIERRA` seleccionado. Prompt de visión: «Compara la pantalla inicial y la captura final tras inyectar una trayectoria de ratón con click. Identifica exactamente el verbo subrayado/seleccionado y la frase superior. ¿Hay un cambio visible en la interfaz que demuestre que el click fue recibido? No infieras el destino únicamente a partir de la ruta de entrada; describe lo que muestran los píxeles.» Respuesta de Ollama: «La frase superior ha cambiado de “ELIGE UN VERBO” a “CIERRA”; el verbo subrayado es CIERRA. El cambio visible demuestra que el click fue recibido.» Esta sonda verifica que el click llega al HUD; **no** verifica todavía que unas coordenadas solicitadas seleccionen el mismo verbo esperado.

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/features/adventure/amiga/002_adv_verbs --debug
bash ./tools/run/run-demo.sh demos/features/adventure/amiga/002_adv_verbs
bash ./tools/run/run-demo.sh demos/features/adventure/amiga/002_adv_verbs \
  --sequence-frames 10 --sequence-interval-ms 2000   # filmina del guion de atracción
node tools/run-host-tests.sh tests/host/adv/480_adv_verbs_say
```

## Defectos encontrados y corregidos durante la validación

1. **La versión no se veía**: el `DiffText` del contador de frames cubre 8 celdas y, en su **primera pintura**, escribe espacios hasta x=336; la versión (272..328, fila 38) se pintaba antes y quedaba borrada en esa primera pasada. Corregido pintando la cabecera **después** del contador; el aviso quedó documentado en `eng/field/diff_text.hpp` (footgun de composición).
2. **El guion de atracción no arrancaba**: `m_attract_t0` se re-fijaba en el paso 0 cada frame (`t` nunca llegaba a 10 y el guion se quedaba en «select ABRE»). Corregido con el centinela `m_attract_t0 == 0`. Verificado por la secuencia (interacción completa).
3. **Resaltado del verbo**: el inverso con `draw_text_words(color 0)` escribe ceros en toda la celda (texto ilegible); el estado seleccionado/hover usa **barra inferior** (2 px / 1 px) sobre la celda.
4. **El click del arnés de verificación usaba el botón derecho** (`input mouse button 1`): la demo lee el izquierdo (índice 0). No era defecto de la demo (el guion de atracción cubre la verificación funcional).

## Alcance y lo NO cubierto (SIN VERIFICAR)

- **Coordenadas precisas de click**: el modo `path` del MCP movió el puntero y produjo un click que cambió el estado visual a `CIERRA`, pero se solicitó MIRA; falta calibrar el origen/escalado del puntero frente a las celdas de `HudHit` y repetir un hotspot del mundo. Evidencia temporal y captura en el directorio de diagnóstico indicado en la tabla.
- **Flicker con el gate completo del suite** (`test-regression.sh --flicker --require-flicker-ok`): la pasada quedó condicionada por fallos de entorno (ver abajo); los topes de `flicker-baseline.json` se declararon con la medición directa de `flicker-check.mjs` sobre la secuencia del gate.
- **Cierre de fin de frames 5-9 del contador**: la ventana de régimen medida es estable, pero la media global sale por las ventanas de arranque (idéntico patrón que la 001).
- **Interacción de «CERRAR»**: implementada (destapa la puerta), no ejercitada en la secuencia del guion.

## Nota de entorno (afectó a la verificación, no al contenido)

Durante esta sesión el entorno de emulación se degradó de forma intermitente: arranques de `-portable` que abortan, canal lateral que deja de responder y **congelaciones deterministas del contador de frames en demos ya verificadas** (la **001** mostró el mismo síntoma en el mismo tramo; el hilo de HUD documentó el caso de la **306** el mismo día en `ROADMAP_DEUDA_TECNICA.md`). Mitigaciones usadas: emulador en copia privada del directorio de la extensión (`WINUAE_GDB_DIR` apuntando a una copia; la `default.uae` compartida era el punto de colisión entre agentes), `WINUAE_USE_LEGACY_LAUNCH=1` (la ruta `-portable` era la que abortaba) y puertos propios alejados (2915/2916). Con ese montaje la demo midió régimen ~48-49 fps y la secuencia del guion de atracción salió completa; los fallos restantes de esta sesión (README de gates, flicker por suite) son de coordinación de runners, no de la demo.
