# Guías de optimización

Optimización de código C++ y del chipset para las plataformas soportadas
(68000/Amiga primero). Organización canónica en `docs/STRUCTURE.md` §10.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [METODO_OPTIMIZACION.md](METODO_OPTIMIZACION.md) | **Método de campaña** (obligatorio antes de optimizar o escribir código nuevo con impacto en rendimiento): principios (el coste es la especificación, presupuesto y mapa de contención antes de diseñar), fases F0–F5 con gates, tabla de presupuesto diferencial contra la referencia, protocolo de A/B con hot-patch y criterio de falsación, modelar vs medir, la referencia como oráculo, disciplina multi-sesión y checklist. |
| [OPTIMIZACION_TILED_ANIMADO.md](OPTIMIZACION_TILED_ANIMADO.md) | Catálogo coste/beneficio de capas de tiles animadas (supresión por visibilidad, máscara compartida, dedup de frames, opacidad por frame, clusters de restore, canal C), con medidas de 207, criterios de aceptación y `tools/debug/bench-demo.mjs`. |
| [PRESUPUESTO_PRIORITARIO_FRAME.md](PRESUPUESTO_PRIORITARIO_FRAME.md) | Diseño del buffer de trabajos por prioridad y deadline de VBlank: clases `Essential`/`Gameplay`/`Decor`/`Idle`, perfiles `preserve50`/`target25`, preempción entre lotes, aging anti-starvation y encaje en `FramePlan`/`eng::os`. |
| [OPTIMIZACION_GPP_68000.md](OPTIMIZACION_GPP_68000.md) | Cómo compila g++/elf2hunk para 68000: reglas de tamaño vs velocidad, `-Os`, cuándo `-O1` engaña y warnings de `-Wextra`. |
| [METODOLOGIA_PROFILING.md](METODOLOGIA_PROFILING.md) | Pipeline de medida (fps → secciones → muestras de CPU → bus), herramientas, trampas conocidas y optimizaciones identificadas con su evidencia. |
| [ATRIBUCION_DE_CICLOS.md](ATRIBUCION_DE_CICLOS.md) | **Dónde se van los ciclos** (medido): coste por operación del engine (fill_rect ≈ 6,5k/llamada, job de Blitter ≈ 3-5k, materialize ≈ 2,3k/intención, accesos a Chip bajo display DMA), antipatrones recurrentes con checklist y casos medidos (085: 426,7k → 142,1k; 110: atribución y rediseño pendiente). |
| [MINIFLOAT16_SUMA_RESTA.md](MINIFLOAT16_SUMA_RESTA.md) | Referencia de investigación del `+`/`-` de `MiniFloat16`: modelo de coste, técnicas ya implementadas (early-out, tabla de renormalización) y candidatas (camino rápido `de == 0`, máscara de alineación). |
| [_probe_gpp68000.cpp](_probe_gpp68000.cpp) | Sonda de compilación usada para medir el código generado (complementa el doc). |

## Reglas obligatorias
- Reglas de rendimiento, comentarios de optimización y port a asm: §12 de
  [OPTIMIZACION_GPP_68000.md](OPTIMIZACION_GPP_68000.md).
- Estilo y restricciones de diseño: `docs/engine/architecture/CODING_STYLE.md`.