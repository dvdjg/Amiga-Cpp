# Roadmap: deuda técnica

Registro de deuda técnica **transversal** detectada durante el desarrollo (no ligada a una única feature/roadmap de dominio). Fuentes y reglas:

- Cada entrada lleva: **síntoma**, **evidencia objetiva** (medida reproducible), **impacto** en la regla correspondiente, **plan por fases** y **estado**.
- Las demos que no cumplen `docs/guides/roadmap/ROADMAP_UNIFICADO.md` §«Objetivos de rendimiento y estabilidad visual» quedan **NO VERIFICADAS** hasta cerrar su entrada.
- Una demo con capturas incoherentes no se marca como validada: la validación visual exige contraste con la intención y verificación por el agente (ver `docs/guides/methodology/DEMO_VISUAL_DEBUG.md`).

## DT-001 · Demo 110 (`ylimited_shooter`): scroll, cadencia y FG rotos

**Estado: ABIERTA — F1 hecho (atribución por ablación); F2 reclasificado como bug de engine (DT-006).** Reportada por el usuario tras ver la demo en WinUAE; confirmada por medidas.

Síntomas (reporte en vivo + medidas propias):

1. **Cadencia muy por debajo de 50 fps**: `measure-fps` da **11,53 fps / 4,34 fields/frame** (615.160 ciclos/frame) en `A500_debug` y **14,63 fps / 3,42 fields** (484.990 ciclos/frame) en `A500_release`. El presupuesto PAL es 1 field (~141,9k ciclos). Evidencia: `out/playfield/...` JSONs de `tools/debug/measure-fps.mjs` (detalle `0x110000ee`/`0x11000046`).
2. **Scroll roto**: la banda inferior de 64 líneas Amiga (y=224..287) está **completamente estática** mientras el resto scrollea (barrido por bandas de 16 px sobre una secuencia de 40 frames: `diff_medio=0.00` solo en y=0..15 y y=448..575 del PNG 2x). El usuario ve además **tres bandas de tiles abajo que no se mueven y parecen bugeadas** (pendiente de mapear a línea Amiga exacta con captura de la zona).
3. **Sin scroll fino horizontal**: la política configurada es `AxisPolicy::Finite` («puntero directo; no repinta»), que no ofrece fine scroll; el vaivén X de la cámara se ve a saltos.
4. **Flicker de FG**: la torreta verde (y≈36) desaparecía/queda a medias en ~50% de las capturas y la nave era invisible (`ship_y = 208` fuera de un lienzo de 208 filas). Mitigado a nivel de demo (nave visible, torreta repinta sin borrar el cuerpo, franja de 1 px en la nave), pero el lienzo FG es **single-buffer** y se actualiza durante el barrido: el fix real es doble buffer del FG con flip en la copperlist.
5. **Sprites parpadeando** (reporte del usuario): pendiente de caracterizar por separado (¿FG single-buffer, caída de frames, o sprites HW del BG?).

Causa raíz conocida hoy: la demo/engine hace **mucho más trabajo por frame que el presupuesto** (3-4 fields) y pinta capas vivas sin doble buffer; con frames de 3-4 fields, cualquier escritura durante el barrido produce tearing/parpadeo visible.

Plan **incremental** (una fase por pasada, cada una con medida y validación):

- **F1 · Perfil y presupuesto. HECHO (ablación):** sin el bloque FG la demo pasa a **39,9 fps / 1,25 fields / 178k ciclos**; con FG, 485k → **el FG cuesta ~307k ciclos/frame (63%)** y el resto ~178k (25% sobre presupuesto). `profile.mjs` no resuelve `g_eng_prof` en estos builds (DT-008). Camino: rediseñar el FG (pre-render + Blitter/BOB o sprites) y recortar el resto.
- **F2 · Scroll. RECLASIFICADO:** las «tres bandas» inferiores son un **bug de engine de la familia XLimited** (la 202 las tiene igual) → **DT-006**. No es de la demo.
- **F3 · Fine scroll X.** Definir el contrato (`Finite` con `BPLCON1` fino) o documentar la limitación; la demo debe moverse suave.
- **F4 · FG sin tearing y en presupuesto.** El lienzo FG single-buffer dibujado por CPU es a la vez el hotspot (63%) y la fuente del flicker; candidato: pre-render + blit o canal de sprites/BOB.
- **F5 · Validación.** `measure-fps` a 50 fps, gate de flicker con baseline real, secuencia **determinista** (`--sequence-step-frames`) + prompts de visión (`DEMO_VISUAL_DEBUG.md` §6.4) + `check-elements --expect`.

Restricción de método: **no tocar a la vez scroll y rendimiento**; cada fase cierra con medida (fps/ciclos), captura y visión.

## DT-002 · Presencia de elementos en la validación — RESUELTO

`tools/analyze/check-elements.mjs` admite `--expect <color>=<min>`: **falla** si en algún frame el número de píxeles de un color esperado baja del mínimo (gate de presencia de elementos). Uso obligatorio en F3/F4 del procedimiento (p. ej. `... 00ff00,ff4400 --expect ff4400=250 --expect 00ff00=900`). Complementa a `flicker-check`, que mide parpadeo de bloques y no pérdida de elementos.

## DT-003 · Protocolo de prompts de visión (referencia) — RESUELTO (ampliado)

Escrito en `docs/guides/methodology/DEMO_VISUAL_DEBUG.md` §6.4 (tres pases: A inventario **sin contexto**, B dinámica/seguimiento, C dirigida con la intención; prompts exactos, reglas —aviso de contenido bloqueante, sin coordenadas de píxel, guardar respuesta cruda— y límites medidos del modelo). El procedimiento completo que lo usa en cada demo/juego es `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` (fases F0–F5 y registro de errores ajenos al turno).

**Ampliación (informe 110):** el modelo **no atiende de forma fiable varias imágenes por mensaje** — con 4 imágenes declaró «ambos frames» y con 2 declaró «un único frame» — y da **falsos negativos en movimiento lento/patrones periódicos** («nave estática», «fondo fijo» con bbox y traslación demostrando lo contrario). Estrategia operativa en §6.4: una imagen por llamada o **hoja de contacto etiquetada** (una imagen), y la **evidencia objetiva decide** (`tools/analyze/check-elements.mjs`, `band-diff`, bbox/traslación). Informe de la demo: `demos/techniques/amiga/playfield/110_ylimited_shooter/VALIDATION.md`.

## DT-004 · `measure-fps` no resuelve símbolos en algunas demos

`measure-fps 202_xlimited_dpf A500_release` falla con `runtime=0x0`/«no se pudo resolver g_eng_run_status» (map/baseText), dejando sin control comparativo a 110. **Plan:** revisar la resolución por `.map`/`qOffsets` para configs release de esa familia o dejar el control con `A500_debug`.

## DT-005 · Captura de secuencia no determinista — RESUELTO

El runner ya soporta **`--sequence-step-frames N`** (`tools/run/run-demo.ts`): captura N frames **1 frame de juego aparte** con el breakpoint `eng_debug_ready_probe` (`frame_NNN_fNNNN.png` consecutivos). Es el modo obligatorio para validar movimiento/cadencia; el modo por intervalo queda para vistazos. Las medidas de `band-diff`/`check-elements` deben hacerse sobre la captura por paso.

## DT-006 · Bandas de tiles inferiores estáticas (familia XLimited)

**Síntoma (usuario):** tres bandas de tiles abajo que no se mueven y parecen bugeadas. **Evidencia:** la 110 y la **202** (misma familia, `display_height=288`, `viewport_h=208`, `Ring`) muestran la zona inferior estática en el barrido por bandas; el **screenshot interno recorta esa zona en negro** (no la muestra), luego solo se ve en la ventana. **Impacto:** render incorrecto en la familia (201/202/110) y auditoría visual que el screenshot no revela. **Estado:** causa sin confirmar; siguiente paso leer `xlimited_composer.hpp`/`xlimited_base.hpp` (DIW/ring staging) y el AHRM/WinUAE antes de tocar; test objetivo: `band-diff` de la zona baja en captura de ventana.

## DT-007 · Demo 085 a 16,6 fps (3 fields) y mancha blanca del disco

`measure-fps 085_copper_plan_scene A500_debug` = **16,62 fps / 3,01 fields / 426.733 ciclos**. El conteo objetivo de blanco alterna 1088↔2204 (frames 1 y 5) y la visión señala una **mancha blanca irregular** en el borde del disco. Hotspot candidato: `RasterGradientEffect::rebuild()` (2 `mul` + 2 `mod` por reconstrucción). Informe: `demos/techniques/amiga/copper/085_copper_plan_scene/VALIDATION.md`.

## DT-008 · `profile.mjs` no resuelve `g_eng_prof`

El símbolo vive en `.gnu.linkonce.b._ZN3eng5debug10g_eng_profE` y el `.map` no le da dirección resoluble por el script (`mapSections` solo mira `.text/.rodata/.eh_frame/.data/.bss`), así que el perfil por secciones no arranca. Mientras se arregla (o se instrumentan secciones del bucle XLimited), la atribución de coste se hizo por **ablación** (desactivar un elemento y medir) — método documentado en el informe de la 110.
