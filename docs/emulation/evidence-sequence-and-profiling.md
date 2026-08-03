# Evidencia de Secuencias y Profiling (WinUAE + IA)

## Objetivo

Pasar de evidencia estática (una captura) a evidencia temporal con trazabilidad:

- que se mueve,
- con que trayectoria,
- que aparece y desaparece,
- en que frames concretos.

## Script operativo actual

- [scripts/run-sequence-evidence.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/run-sequence-evidence.mjs)

Este script:

1. captura una secuencia de frames desde el buffer interno de WinUAE;
2. toma telemetría low-level cada N frames (`PC`, `VPOSR/VHPOSR`, `DMACONR`, `BPLCON0`);
3. analiza ventanas de frames con `qwen2.5-vl-7b-instruct`;
4. genera timeline estructurado;
5. opcionalmente solicita perfil low-level con `winuae_profile`.
6. genera correlación automática entre eventos visuales y contexto `runtime/evidence_log`.

También puede invocarse desde el runner común:

- [scripts/run-battery-case.mjs](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/scripts/run-battery-case.mjs) con `--sequence`.

## Comando recomendado (arranque inmediato)

```bash
node scripts/run-sequence-evidence.mjs \
  --evidence-dir tests/amiga-battery/T02_lores_32c/evidence \
  --frames 64 \
  --frame-delay-ms 40 \
  --window-size 8 \
  --telemetry-every 4 \
  --profile-frames 24 \
  --capture-mode internal_buffer \
  --vision-context "T02 lattice motion validation" \
  --expected-motion "Stable lattice with no unintended flicker or disappear/reappear artifacts"
```

Si quieres forzar boot de un ADF concreto antes de capturar:

```bash
node scripts/run-sequence-evidence.mjs \
  --evidence-dir tests/amiga-battery/T02_lores_32c/evidence \
  --adf out/battery_T02.adf \
  --force-boot
```

## Artefactos generados

```text
evidence/
  sequence/
    frames/
      f000001.png
      f000002.png
      ...
    sequence-index.json
    sequence-vision.json
    sequence-vision.md
    sequence-events.json
    sequence-correlated-events.json
    sequence-timeline.md
    sequence-profile.amigaprofile
    sequence-profile.parsed.json
    sequence-summary.json
```

## Estrategia de análisis visual (IA)

El modelo local se usa por ventanas cortas (8-16 frames), no sobre "todo el video de golpe".

Ventajas:

- reduce alucinaciones,
- mejora precisión de eventos,
- permite mapear cada evento a un rango de frames concreto.

Formato esperado de evento:

- `type` (`motion`, `spawn`, `despawn`, `palette_change`, `other`)
- `description`
- `start_frame`
- `end_frame`
- `confidence`

## Profiling low-level

Si, se puede pedir profiling desde MCP (`winuae_profile`) y analizarlo:

1. capturar perfil en una ventana temporal acotada;
2. guardar artefacto crudo;
3. extraer resumen parseado cuando el formato lo permita;
4. correlacionar con timeline visual.

Nota: si el archivo de perfil llega en formato binario no JSON, el pipeline lo conserva como evidencia cruda y marca el parseo como parcial.

## Criterio de adopcion inicial

Se considera "operativo" cuando en una tanda se obtiene:

1. secuencia completa de 64 frames,
2. eventos visuales estructurados por ventanas,
3. timeline final con eventos trazables a frames,
4. telemetría low-level asociada.
