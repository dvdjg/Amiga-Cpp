# Roadmap: deuda técnica

Registro de deuda técnica **transversal** detectada durante el desarrollo (no ligada a una única feature/roadmap de dominio). Fuentes y reglas:

- Cada entrada lleva: **síntoma**, **evidencia objetiva** (medida reproducible), **impacto** en la regla correspondiente, **plan por fases** y **estado**.
- Las demos que no cumplen `docs/guides/roadmap/ROADMAP_UNIFICADO.md` §«Objetivos de rendimiento y estabilidad visual» quedan **NO VERIFICADAS** hasta cerrar su entrada.
- Una demo con capturas incoherentes no se marca como validada: la validación visual exige contraste con la intención y verificación por el agente (ver `docs/guides/methodology/DEMO_VISUAL_DEBUG.md`).

## DT-001 · Demo 110 (`ylimited_shooter`): scroll, cadencia y FG rotos

**Estado: ABIERTA (en arreglo por fases).** Reportada por el usuario tras ver la demo en WinUAE; confirmada por medidas.

Síntomas (reporte en vivo + medidas propias):

1. **Cadencia muy por debajo de 50 fps**: `measure-fps` da **11,53 fps / 4,34 fields/frame** (615.160 ciclos/frame) en `A500_debug` y **14,63 fps / 3,42 fields** (484.990 ciclos/frame) en `A500_release`. El presupuesto PAL es 1 field (~141,9k ciclos). Evidencia: `out/playfield/...` JSONs de `tools/debug/measure-fps.mjs` (detalle `0x110000ee`/`0x11000046`).
2. **Scroll roto**: la banda inferior de 64 líneas Amiga (y=224..287) está **completamente estática** mientras el resto scrollea (barrido por bandas de 16 px sobre una secuencia de 40 frames: `diff_medio=0.00` solo en y=0..15 y y=448..575 del PNG 2x). El usuario ve además **tres bandas de tiles abajo que no se mueven y parecen bugeadas** (pendiente de mapear a línea Amiga exacta con captura de la zona).
3. **Sin scroll fino horizontal**: la política configurada es `AxisPolicy::Finite` («puntero directo; no repinta»), que no ofrece fine scroll; el vaivén X de la cámara se ve a saltos.
4. **Flicker de FG**: la torreta verde (y≈36) desaparecía/queda a medias en ~50% de las capturas y la nave era invisible (`ship_y = 208` fuera de un lienzo de 208 filas). Mitigado a nivel de demo (nave visible, torreta repinta sin borrar el cuerpo, franja de 1 px en la nave), pero el lienzo FG es **single-buffer** y se actualiza durante el barrido: el fix real es doble buffer del FG con flip en la copperlist.
5. **Sprites parpadeando** (reporte del usuario): pendiente de caracterizar por separado (¿FG single-buffer, caída de frames, o sprites HW del BG?).

Causa raíz conocida hoy: la demo/engine hace **mucho más trabajo por frame que el presupuesto** (3-4 fields) y pinta capas vivas sin doble buffer; con frames de 3-4 fields, cualquier escritura durante el barrido produce tearing/parpadeo visible.

Plan **incremental** (una fase por pasada, cada una con medida y validación):

- **F1 · Perfil y presupuesto.** `tools/debug/profile.mjs 110_ylimited_shooter` + análisis con IA local; localizar los 3,4 fields (¿scroll blits, compose, FG CPU en -O0, self-tests?). Bajar a 1 field. Comparar con control (202/203) cuando `measure-fps` resuelva símbolos (ver DT-004).
- **F2 · Scroll.** Leer `PLAYFIELD_SCROLL_ARCHITECTURE.md` y el modo `Ring`/`Finite`; identificar por qué las 64 líneas inferiores no se actualizan y qué son las tres bandas bugeadas; decidir si es config de la demo, límite del motor o bug (con caso de test).
- **F3 · Fine scroll X.** Definir el contrato (`Finite` con `BPLCON1` fino) o documentar la limitación; la demo debe moverse suave.
- **F4 · FG sin tearing.** Estudiar doble buffer del lienzo FG (el composer ya dobla copperlist); si es motor, implementar; si no, redibujado mínimo en vblank.
- **F5 · Validación.** `measure-fps` a 50 fps, gate de flicker con baseline real, secuencia densa + prompts de visión (`DEMO_VISUAL_DEBUG.md` §6.4).

Restricción de método: **no tocar a la vez scroll y rendimiento**; cada fase cierra con medida (fps/ciclos), captura y visión.

## DT-002 · `flicker-check` no cazó la torreta de 110

`tools/vision-review/flicker-check.mjs` reportó `0 candidatos` sobre una secuencia en la que la torreta se perdía/queda a medias en varias capturas (conteo de color: verde 0/parcial en 2-6 de cada 6-40 frames según captura). El gate muestrea pocos frames y clasifica zonas en movimiento coherente; una **pérdida intermitente de un elemento pequeño** no entra en su definición de candidato. **Impacto:** un gate «OK» no implica contenido correcto; la visión y el conteo de elementos siguen siendo obligatorios. **Plan:** añadir al gate (o a la secuencia) un chequeo de **presencia de elementos esperados** por color/región, o documentar su alcance como «solo parpadeo de bloques».

## DT-003 · Protocolo de prompts de visión (referencia) — RESUELTO

Escrito en `docs/guides/methodology/DEMO_VISUAL_DEBUG.md` §6.4 (tres pases: A inventario **sin contexto**, B dinámica/seguimiento, C dirigida con la intención; prompts exactos, reglas —aviso de contenido bloqueante, sin coordenadas de píxel, guardar respuesta cruda— y límites medidos del modelo). El procedimiento completo que lo usa en cada demo/juego es `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` (fases F0–F5 y registro de errores ajenos al turno).

## DT-004 · `measure-fps` no resuelve símbolos en algunas demos

`measure-fps 202_xlimited_dpf A500_release` falla con `runtime=0x0`/«no se pudo resolver g_eng_run_status» (map/baseText), dejando sin control comparativo a 110. **Plan:** revisar la resolución por `.map`/`qOffsets` para configs release de esa familia o dejar el control con `A500_debug`.

## DT-005 · Captura de secuencia no determinista

La captura de frames (100/20 ms) muestrea mientras el juego corre: las imágenes pueden pillarse a mitad de update (se ve en conteos de color parciales y en `sequence-analysis`). **Impacto:** conclusiones falsas de «flicker» o «roto» a partir de artefactos de captura, y viceversa. **Plan:** captura determinista (pausa/step por frame o captura tras frontera de frame) para los análisis de contenido.
