# Invariantes hardware y dónde se verifican

Índice de verdades del hardware Amiga (OCS/ECS) que el engine **asume**, con el punto donde cada
una se demuestra o se explica. Sirve de checklist para no depender solo de efectos visuales
compuestos y para saber qué falta por verificar.

Cada invariante se enuncia como una afirmación comprobable; "Evidencia" apunta a la demo/test/doc
que la respalda, o "pendiente" si aún no hay un caso que la aísle.

| ID | Invariante | Evidencia |
|---|---|---|
| MI01 | Un bitplane leído por DMA debe residir en **CHIP RAM**; un `BPLxPT` fuera de CHIP no es válido. | `MEMORY_MODEL.md`; todas las demos (arena Chip). |
| MI02 | Actualizar `COP1LC` una vez por frame (sin `COPJMP1`) hace entrar la lista nueva en el siguiente frame, sin franjas mid-frame. | `amiga-a500-dma-copper-state-rules.md`; `MinimalBackend::install_copper_list`. |
| MI03 | Forzar `COPJMP1` durante el barrido reinicia el Copper y parte el raster visible. | `DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md`; comentario de `install_copper_list`. |
| MI04 | El `WAIT` del Copper compara 8 bits de línea (0..255); por encima de 255 hay que manejar el overflow vertical o el cambio cae en línea incorrecta. | `AMIGA_8WAY_SCROLLING.md` §12; split del compositor single. |
| MI05 | `BPLCON1` (fino) y el avance de punteros (coarse) son complementarios; ninguno resuelve el scroll por sí solo. | Demo `101_ehb_tile_scroll_driver` (resuelto); `modulo-tricks.md`. |
| MI06 | El wrap vertical se consigue con `BPL1MOD`/`BPL2MOD`; el cambio de módulo debe ocurrir en la línea correcta y sin franja. | `DPF_MIXTO_SPLIT_LINEAL.md`; demo `202`; `modulo-tricks.md`. |
| MI07 | Un sprite hardware se puede **reprogramar varias veces en el mismo frame** (mismo canal, distintas líneas). | Demo `053`; `sprite-layer.md`; `SpriteManager`/`SpriteAllocator`. |
| MI08 | Cambiar `SPRxPOS`/`SPRxCTL` o el puntero de sprite durante el scanout reutiliza el canal como "ventana temporal", no como sprite fijo. | Demos `053`/`054`; `sprite-layer.md`. |
| MI09 | Cambiar la **geometría de vídeo** (`BPLCON0`/`DDF`/`BPLxMOD`) en un `WAIT` permite tramos con distinto número de planos, reprogramando en orden `BPLCON0`→`DDF`→modulos→`BPLxPT` y con el `DDF` alineado. | **pendiente** de microtest (HUD de 2/3/4 planos bajo split); `ModeSwitchZone` en `PLAYFIELD_SCROLL_ARCHITECTURE.md` §4.1. |
| MI10 | En HAM, el **cuadruplicado de líneas** (mismo par de planos leído 4 veces con `BPLxMOD`/`BPLCON1` alternos) y los cambios de `COLORxx` por línea son listas de Copper válidas. | **pendiente**; `FIRE_RGB_PORT_PLAN.md`, `C2P_BLITTER.md`. |

## Cómo añadir una invariante

- Formularla como una afirmación exacta (registro, bits, condición) y no como "una demo".
- Dar la **evidencia mínima** reproducible: microtest propio, demo, o doc de referencia verificado.
- Si es una técnica (no una verdad del hardware), vive en `docs/reference/amiga/techniques/` y se
  enlaza aquí, no se duplica.

Los puntos **pendientes** (MI09, MI10) son candidatos del roadmap
`docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md` (MI09) y del port del fuego HAM (MI10).
