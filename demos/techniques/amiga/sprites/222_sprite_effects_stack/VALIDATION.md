# VALIDATION — 222_sprite_effects_stack (efectos apilados, F4.2+F7)

Informe de validación de la **composición de efectos**: bending + palette splitting sobre los mismos sprites HW (canales 2/3). Campaña: 2026-10-13, A500_debug (WinUAE-DBG).

## Qué se pretendía

1. Dos columnas 16×64 dobladas por línea (bending) **y** con cambio de paleta a mitad de altura (cyan arriba, magenta abajo), en la misma copperlist.
2. Los dos efectos sobre los mismos canales, sin interferirse (cada fila conserva su X y su color).
3. 50 fps sin artefactos.

## Medidas

| Métrica | Valor |
|---|---|
| `measure-fps.mjs` (20 s) | **49.92 fps** emulado; 1000 frames; **1.00159** campos/frame |

## Evidencia de visión (Ollama `qwen3-vl:8b-instruct-q8_0`, 2 capturas)

Respuesta cruda resumida:

> 1) **2 columnas onduladas (forma de V invertida o lambda)**. 2) **Sí, cambio de color: cian/blanco arriba, magenta abajo**. 4) **No se observan cortes, basura ni líneas sueltas**… bordes bien definidos.

Inspección directa de las capturas: las dos mitades de cada columna se distinguen con nitidez y la onda es continua a través del cambio de color (la fila de la conmutación no rompe la silueta).

## Conclusión (alcance exacto)

- **VERIFICADO**: acumulación bending + conmutación de paleta por Copper sobre los mismos sprites HW (par 2/3), con 49.92 fps/1.00159 campos y sin artefactos; aplicable a cualquier canal (221 lo hace sobre el par 0/1).
- **NO CUBIERTO**: combinación con cadenas DMA en el mismo canal (bending = un sprite por canal; las cadenas multiplexan varios objetos por canal — son caminos alternativos por diseño, no apilables en el mismo canal); combinación con scroll de playfield (familia del pacing de 215).

## Reproducir

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/222_sprite_effects_stack --debug
node tools/debug/measure-fps.mjs 222_sprite_effects_stack A500_debug --json
```
