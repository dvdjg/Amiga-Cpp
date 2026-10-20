# Validación — 086_palette_checkerboard

## Alcance y resultado

La comprobación cubre el display A500 en WinUAE: bitmap 320×256, cuatro bandas, paletas RGB444 de 8/16/32 colores y 32 bases EHB, matrices de 8²/16²/32²/64² parejas, swatches, rótulos y contador de frame. El Copper usa una lista estática de 306 palabras; `RunStatus.detail=0x132` confirma que se publicó esa lista. El setup pasa `scheduler.ok()` y `eng::setup::check_copper_list` antes de `READY`.

## Comandos y medidas

```powershell
& 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard --debug --clean
& 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard --sequence-frames 6 --sequence-interval-ms 1000
& 'C:\Program Files\Git\bin\bash.exe' ./tools/analyze/analyze-demo.sh demos/techniques/amiga/copper/086_palette_checkerboard
node tools/debug/measure-fps.mjs 086_palette_checkerboard A500_debug --json
node tools/analyze/vision-final.mjs 086_palette_checkerboard out/run/086_palette_checkerboard/A500_debug/sequence --demo-dir demos/techniques/amiga/copper/086_palette_checkerboard
```

Build: correcto para `A500_debug` con GCC 15.1.0. Analyze: `OK`, captura 756×576. Medida: 49,92 FPS emulados, 49,84 FPS host, 142102 ciclos/frame, 1,00159 campos/frame, 1012 frames; ocho ventanas entre 49,52 y 50,71 FPS, todas cuantizadas a un campo. La salida visual cambia únicamente en el rectángulo del contador inferior derecho; en la secuencia de seis capturas los cambios por pareja fueron 168, 116, 144, 188 y 148 píxeles, siempre dentro de la caja `(524,532)..(569,545)`. Las matrices, swatches y rótulos permanecieron sin cambios.

## Visión

Modelo: `qwen3-vl:8b-instruct-q8_0`, Ollama 0.32.14. La evaluación sin pistas y su respuesta cruda están en [`src/VISION.md`](src/VISION.md). La evaluación dirigida detectó los cuatro modos, sus matrices completas y los swatches sin solapes ni recortes; la interpretación de la proximidad a la diagonal se considera orientativa, porque la clasificación cuantitativa se basa en luminancia y no en una métrica de diferencia cromática. El OCR visual del VLM confundió algunos dígitos hexadecimales pequeños; las medidas exactas del contador se corroboran con `RunStatus` y el diff RGB, no con el texto reconocido por el modelo.

### Pase A — inventario sin contexto

Prompt: `Las 3 imágenes adjuntas van EN ORDEN TEMPORAL: imagen 1 = frame f0027, imagen 2 = frame f0199, imagen 3 = frame f0312. Describe only what is visible: enumerate the distinct bands, checker patterns, palette samples and readable text. Do not infer program purpose. Report any clipping, overlap, malformed text, or inconsistent region boundary.`

Respuesta cruda:

```text
The three images display identical content, which is a static screen from a color test pattern program, likely for a video game console or computer system. There is no visible temporal evolution or change between the frames. The screen is composed of the following elements:

- **Text:** White, fixed-width, uppercase text against a black background. The text is consistent across all three images and reads:
  - Top left: `3BPL 8 COLORS`, `ALL 8X8 PAIRS`, `DIAG: SIM LIGHT`, `FAR: BRIGHT GAP`
  - Middle left: `4BPL 16 COLORS`, `ALL 16X16 PAIRS`, `DIAG: SIM LIGHT`, `FAR: BRIGHT GAP`
  - Middle right: `5BPL 32 COLORS`, `ALL 32X32 PAIRS`, `DIAG: SIM LIGHT`, `FAR: BRIGHT GAP`
  - Bottom right: `COLOR PAIRS`, `EHB 64 COLORS`, `DIAG: SIM LIGHT`, `FAR: BRIGHT GAP`, `v1.0.5655`, `ALL 64X64 PAIRS`, `2X2 A/B CHECKER`
  - Bottom right corner: `FR=000000A0` (image 1), `FR=00000138` (images 2 and 3).
- **Visual Patterns:** A vertical color bar runs along the far left edge, displaying a spectrum of colors. The small 8×8, 16×16, and 32×32 palette samples and the large 64×64 checkerboard use fine alternating color patterns.
- **Clipping, Overlap, Malformed Text or Inconsistent Boundaries:** There is no visible clipping, overlap, or malformed text. The region boundaries are consistent across all three images.
```

### Pase B — seguimiento del contador

Prompt: `Las 3 imágenes adjuntas van EN ORDEN TEMPORAL: imagen 1 = frame f0027, imagen 2 = frame f0199, imagen 3 = frame f0312. Compare these frames in temporal order. Focus only on the bottom-right frame counter and four checker matrices: does the counter advance, and do any matrix, label, or swatch change, flicker, or shift?`

Respuesta cruda:

```text
Comparing the three frames in temporal order (f0029 → f0200 → f0315), the following observations are made:

- **Frame Counter**: The counter in the bottom-right corner advances as expected. It changes from `FR=000000A8` in the first frame to `FR=00000138` in the second frame. The third frame is not provided, but the counter’s progression indicates the sequence is valid.
- **Checker Matrices**: The four checker matrices (3BPL, 4BPL, 5BPL, and the large 64-color checker) are identical in all three frames. There are no visual changes, flickers, or shifts in their patterns, colors, or positions. The labels accompanying each matrix are also unchanged.

In summary: the frame counter advances correctly, and all four checker matrices and their associated labels remain static and unchanged across the three frames.
```

Este pase dirigido sí recibió tres frames de la secuencia lateral. La diferencia determinista por píxel confirma el resultado: solo cambia el rectángulo del contador y las matrices permanecen estables.

### Pase C — contraste con la intención

Prompt: `Las 3 imágenes adjuntas van EN ORDEN TEMPORAL: imagen 1 = frame f0027, imagen 2 = frame f0199, imagen 3 = frame f0312. The intended content is four horizontal Copper-switched bands: 3 bitplanes with 8x8 ordered pairs, 4 bitplanes with 16x16, 5 bitplanes with 32x32, and EHB 6 bitplanes with 64x64. Each 2x2 cell alternates two palette indices; luminance-sorted axes make the diagonal similar-brightness pairs and far-off-diagonal pairs larger brightness gaps. For each band, say whether its full matrix, swatch strip, and labels are visible and coherent; report any uncertainty or visible defect.`

Respuesta cruda:

```text
Based on the provided images and the description, here is an analysis of the visual content for each frame, assessing the visibility and coherence of the intended four horizontal bands.

The intended content consists of four horizontal bands, each corresponding to a different bitplane configuration:
1. **3BPL 8 Colors**: All 8×8 pairs.
2. **4BPL 16 Colors**: All 16×16 pairs.
3. **5BPL 32 Colors**: All 32×32 pairs.
4. **EHB 64 Colors**: All 64×64 pairs.

Each band should display a swatch strip and corresponding labels.

**Analysis per Frame**

**Frame f0029 (Image 1)**
- **3BPL 8 Colors**: Visible and coherent. The small 8×8 grid swatch is present, and its label `3BPL 8 COLORS` is clearly visible.
- **4BPL 16 Colors**: Visible and coherent. The 16×16 grid swatch is present, and its label `4BPL 16 COLORS` is clearly visible.
- **5BPL 32 Colors**: Visible and coherent. The 32×32 grid swatch is present, and its label `5BPL 32 COLORS` is clearly visible.
- **EHB 64 Colors**: Visible and coherent. The large 64×64 grid swatch is present, and its label `EHB 64 COLORS` is clearly visible.
- **Overall**: All four bands are fully visible, correctly labeled, and their content appears coherent and correctly rendered. The frame counter `FR=000000A8` (f0029) is visible at the bottom.

**Frame f0200 (Image 2)**
- The 3BPL, 4BPL, 5BPL, and EHB bands, swatches, and labels are visible and correct.
- **Overall**: All four bands are fully visible, correctly labeled, and coherent. The frame counter `FR=00000138` (f0200) is visible at the bottom.

**Frame f0315 (Image 3)**
- The 3BPL, 4BPL, 5BPL, and EHB bands, swatches, and labels are visible and correct.
- **Overall**: All four bands are fully visible, correctly labeled, and coherent. The frame counter `FR=000001B8` (f0315) is visible at the bottom.

**Conclusion**

There is no visible defect or uncertainty in any of the provided frames. All four bands (3BPL, 4BPL, 5BPL, EHB) are consistently and fully visible, correctly labeled, and their corresponding swatches appear coherent and correctly rendered throughout the sequence from frame f0029 to f0315.
```

## Conclusión

Correcto en el alcance probado: compilación y arranque A500, cuatro segmentos visibles y coherentes, 4096 parejas EHB expuestas, contador cambiante sin alteración de las matrices, estabilidad de un campo por frame y ausencia de artefactos señalados en la evaluación visual. SIN VERIFICAR: otros perfiles de chipset (ECS/AGA), diferencias cromáticas entre pares de luminancia similar y calidad perceptual sobre imágenes fuente reales.

La regresión general `tools/test-regression.sh --demo ... --warp` quedó bloqueada antes de ejecutar la demo por una incidencia ajena al cambio: BOM UTF-8 preexistente en `demos/es/dk_port_lazy/src/main.cpp`.
