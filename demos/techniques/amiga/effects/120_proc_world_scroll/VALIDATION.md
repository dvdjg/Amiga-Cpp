# VALIDATION — Demo 120 (`120_proc_world_scroll`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).

**Estado: VERIFICADA EN EL ALCANCE PROBADO.** Contrato de cadencia: **49,91 fps** (1,002 campos/frame, 142142 ciclos/frame; debug, 2026-10) con cero candidatos de flicker. El HUD de depuración vive en el overlay del host (`HostHud`); la franja de 48 px se conserva negra para la geometría del corkscrew. La medida anterior con HUD de 1 bpp en viewport era 48,5–49,2 fps.

Intención esperada: pantalla 320×256 (208 de campo + 48 de HUD) con el mundo cenital
desplazándose horizontalmente 1 px/frame de forma continua (sin costuras ni medio-frame),
banco de ~160 tiles de 16×16 en 5 planos (32 colores), y la franja con la identidad.

## Técnica vigente

- Mundo 40×28 generado **en compilación** (`constexpr`; mismas semillas que la 119/offline:
  huellas FNV cotejadas en `HOST-445`).
- Banco por `indexed_tiles` (builder genérico en planos — se levantó la restricción
  histórica `planes==6`, que era del pipeline EHB; ver `xlimited_scene.hpp`).
- Scroll por `XlimitedScene`+`TileLayerMap` toroidal (semántica probada en `HOST-447`) con
  sub-pasos atómicos de 1 px.
- **HUD de host** (`HostHud`): nombre + número + versión + frame + fps en el overlay; la franja de 48 px queda negra y conserva la geometría del corkscrew. El lienzo lo limpia el engine (`XlimitedScene::begin`). Test: `HOST-486` (HUD) y `HOST-487` (limpieza de lienzos).
- Bucle por **sondeo** (`run_frames_polling`, modo estable del doc 212).

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| **fps vigente (debug, HUD de host)** | `measure-fps 120_proc_world_scroll A500_debug --json` | **49,91 fps**, **1,002 campos/frame**, 142142 ciclos/frame | **49,9 fps estables** (min 49,53 / max 50,31); el HUD ya no trabaja en viewport |
| fps (debug, HUD 1bpp genérico por palabras) | `record-fps 120_proc_world_scroll A500_debug` | `48,51–49,17 fps`, `1,019–1,032 campos/frame` | referencia histórica del HUD en viewport |
| fps (release, HUD 1bpp genérico) | idem `A500_release` | `49,17 fps` | release y debug equivalentes |
| HUD intermedio (parcheo incremental por celdas) | idem | `48,67/48,59 fps` | arreglaba el cuello pero dejaba la maquinaria de diff en la demo; sustituido por el HUD 1bpp |
| Antes del cambio (debug) | `measure-fps … A500_debug` | `37,05 fps`, `1,350 campos/frame`, `191487 ciclos/frame` | el HUD completo (5 planos) costaba ~3,2 M ciclos/refresco (~26 % del presupuesto) |
| Ablación `K120_SKIP_HUD` | build con `EXTRA_DEFINES='-DK120_SKIP_HUD=1'` + `measure-fps` | `50,00 fps`, `1,000 campos/frame` | el loop+scroll+compose+install caben en 1 campo exacto; el HUD era el único cuello |
| Ablaciones `K120_SKIP_COMPOSE/UPDATE/INSTALL` | idem, defines por etapa (medidas con el HUD pesado aún dentro) | `37,05–39,01 fps` (sin cambio) | compose/update/install quedaron **por debajo del ruido** frente al HUD; con el HUD 1bpp (≈1k ciclos/refresco) el residuo del frame completo es ~2,7 k ciclos/frame (1,019 campos) |
| Referencia (201, debug) | `measure-fps 201_ehb_map A500_debug` | `22,59 fps`, `2,213 campos/frame` | su HUD se redibuja solo al cambiar de segmento (~3 s) y su techo es coste per-frame propio (6 planos): **no** comparte el patrón del HUD pesado (hipótesis descartada); su optimización es frente aparte |
| Captura + secuencia | `run-demo --sequence-frames 6 --sequence-interval-ms 1000` | 6 PNG en `out/run/…/A500_debug/sequence/` | mapa desplazado + HUD legible |

## Prompts y respuestas (crudos)

Prompt (traducido del script `out/tmp/eval_hud.sh`, frames 0/3/5):

> «Esta es una demo Amiga con scroll lateral de un mundo procedural y una franja HUD
> inferior con texto tipo "PROC WORLD SCROLL v1.0.NNNN  F NNN". Verifica: 1) el texto del
> HUD se lee completo y correcto, sin dígitos corruptos ni celdas viejas/mezcladas;
> 2) el contador F avanza entre frames; 3) el paisaje scrollea entre frames sin artefactos
> ni huecos.»

Respuesta (qwen3-vl:8b-instruct-q8_0, literal):

> «1) El texto del HUD se lee completo y correcto en ambos frames: "PROC WORLD SCROLL
> v1.0.3863 F 228" y "PROC WORLD SCROLL v1.0.3863 F 256".
> 2) El contador F avanza correctamente: de 228 a 256 en el segundo frame.
> 3) El paisaje scrollea sin artefactos ni huecos: en el primer frame hay una franja azul
> en el centro que se desplaza hacia la izquierda, mientras que en el segundo frame el
> paisaje se ha desplazado completamente, mostrando un nuevo sector con una estructura
> vertical a la derecha y un objeto circular en la esquina superior derecha.»

Regresión:

```
./tools/test-regression.sh --demo demos/techniques/amiga/effects/120_proc_world_scroll \
    --warp --flicker --require-flicker-ok   →  Regression OK (analyze OK; 0 candidatos de flicker)
```

## Conclusiones

1. **Integración verificada**: el mundo procedural se publica con el driver de scroll real
   (banco indexado + corkscrew + `TileLayerMap` toroidal), con HUD y sin fallos de
   instalación; el builder indexado pasó a ser genérico en planos (usado aquí a 5).
2. **Cadencia**: 49,2 fps con el HUD de 1 bitplane por palabras; la ablación
   `K120_SKIP_HUD` demuestra que el resto del frame (loop + scroll + compose + install) cabe
   en 1,000 campos exactos. El rendimiento dejó de ser el cuello de la demo.
3. **SIN VERIFICAR**: cadencia estable en sesiones largas (>10 min); NTSC; máquina distinta
   de A500; cotejo del patrón de HUD de la 201 (hipótesis: repintado completo per-frame).
