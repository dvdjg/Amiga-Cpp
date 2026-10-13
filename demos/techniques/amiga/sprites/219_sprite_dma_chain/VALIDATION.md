# VALIDATION — 219_sprite_dma_chain (emisión DMA encadenada por canal)

Informe de validación de la técnica **F3** (`docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.2): 20 balas en columna servidas por **un solo canal** de sprite, con tres emisiones comparadas (rearme por Copper, cadena DMA reconstruida y cadena persistente parcheada). Fecha de la campaña: 2026-10-13.

## Qué se pretendía

1. La columna de 20 balas se renderiza **completa y uniforme** (sin huecos, sin columnas fantasma) en las tres estrategias, con el **marcador** superior izquierdo identificando cada modo (1 = rearme, 2 = cadena, 3 = persistente).
2. La animación mantiene **50 fps** (un frame por VBlank) sin tirones en las tres.
3. El A/B de coste se lee en la **copperlist publicada**: la cadena debe reducir drásticamente los words de rearmes (un `WAIT` + 4 MOVEs por franja en rearme vs un solo armado con la cadena).

## Medidas (A500_debug, WinUAE-DBG, `measure-fps.mjs`)

| Estrategia | CONFIG_ID | fps emulado | frames/20 s | campos/frame | copper_words (telemetría) |
|---|---|---|---|---|---|
| Rearme por Copper (`SPRITE219_STRATEGY=0`) | `A500_sprite219_strategy0_debug` | **49.92** | 1000 | 1.00159 | **304** |
| Cadena reconstruida (`=1`) | `A500_sprite219_strategy1_debug` | **49.92** | 1001 | 1.00159 | **114** |
| Cadena persistente (`=2`, defecto) | `A500_debug` | **49.92** | 1000 | 1.00159 | **114** |

`copper_words` es `sched.words_used()` leído en runtime por el canal lateral desde `g_eng_frame_telemetry` (mismo mapa de símbolos que la 201/202). La diferencia rearme→cadena es **190 words/frame** (los 19 rearmes: `WAIT` + `SPRxPOS/CTL/PT`), con el mismo resultado visual. El coste CPU adicional de reconstruir la cadena (copia de bloques, modo 1) o parchear (modo 2) no se discrimina con `cyclesPerFrame` porque la demo está sincronizada al VBlank (los ciclos/frame los fija el PAL); su magnitud está acotada por el HOST (builder) y no altera la cadencia (1.00159 campos/frame uniforme en los tres).

## Evidencia de visión (secuencia + modelo local)

Prompts y respuestas crudas (Ollama `qwen3-vl:8b-instruct-q8_0`, vía `tools/analyze/ollama-desc.mjs`), sobre 3 capturas internas de WinUAE separadas ~0.8 s.

**Modo persistente (3 frames, `s219/mode2`)** — respuesta cruda resumida (íntegra en la sesión):

> 1) **Aproximadamente 20 balas**… 2) **Sí, la columna está completa y uniforme. No hay huecos ni balas cortadas.** 3) **Sí, la columna se ha desplazado verticalmente**… 4) **No se observa basura, columnas fantasma ni sprites fuera de la columna.** 5) El rectángulo superior izquierdo es de color azul oscuro.

**Modo rearme (3 frames, `s219/mode0`)** — respuesta cruda resumida:

> 2) Sí, la columna está **completa y uniforme**. No hay huecos ni elementos cortados. 4) **No se observa basura, columnas fantasma ni sprites fuera de la columna principal.** (El conteo del modelo fue 14 en esa pasada; el conteo directo sobre la captura da 20 — se anota la discrepancia y se declara el conteo por inspección directa.)

**Modo cadena reconstruida (1 frame, `s219/mode1`)**: inspección directa — 20 balas completas, marcador azul distinto; sin artefactos.

Marcadores verificados por inspección: esquina superior izquierda en color plano 1/2/3 según modo (rearme / cadena / persistente).

## Conclusión (alcance exacto)

- **VERIFICADO**: las tres estrategias renderizan las 20 balas con separación uniforme y sin columnas fantasma (visión + inspección); las tres a **49.92 fps** con **1.00159 campos/frame** (sin tirones); el **movimiento vertical** se ha verificado por visión en la secuencia del modo persistente (los otros dos, mismas posiciones/estructura por construcción, un solo frame capturado cada uno).
- **VERIFICADO**: el A/B de emisión por **telemetría de copperlist** (304 → 114 words/frame, −190).
- **NO CUBIERTO**: el coste CPU fino de reconstruir vs parchear la cadena (mismo fps; requiere perfil de CPU/DMA por rutina, fuera del alcance de esta campaña); el comportamiento con >20 bloques por canal (probado en HOST hasta el tope del builder).
- **Hallazgo incorporado al engine**: el armado de la cadena debe escribir **POS/CTL de la primera franja** por registro (patrón `arm_object`); armar solo con `SPRxPT` desincroniza el walk cuando la `VSTART` es temprana (observado en la primera iteración de esta demo; corregido en `SpriteManager::arm_dma_chain` y documentado en la cabecera de `sprite_chain.hpp`).

## Cómo reproducir

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/219_sprite_dma_chain --debug
node tools/debug/measure-fps.mjs 219_sprite_dma_chain A500_debug --json

EXTRA_DEFINES=-DSPRITE219_STRATEGY=0 bash tools/build/build-demo.sh \
  demos/techniques/amiga/sprites/219_sprite_dma_chain --debug
bash tools/run/run-demo.sh demos/techniques/amiga/sprites/219_sprite_dma_chain   # genera el runner de esa config
node tools/debug/measure-fps.mjs 219_sprite_dma_chain A500_sprite219_strategy0_debug --json
```

Telemetría por canal lateral: `g_eng_frame_telemetry` (offsets: frame u32@0, blit_jobs u16@4, blit_words u16@6, **copper_words u16@8**).
