# EVALUACIÓN DE VISIÓN — 086_palette_checkerboard

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-20T14:38:21.339Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_001.png, frame_003.png, frame_004.png, frame_005.png (5)
- Secuencia: `out/run/086_palette_checkerboard/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

Las tres imágenes son idénticas en su contenido visual, excepto por un valor numérico en la esquina inferior derecha.

**Descripción general:**
- **Fondo:** Negro sólido.
- **Contenido principal:** Un patrón de prueba de colores compuesto por múltiples secciones gráficas y texto blanco.
- **Elementos gráficos:**
  - **Columna izquierda:** Una barra vertical de colores, con píxeles de distintos tonos.
  - **Área superior izquierda:** Un pequeño cuadrado de patrón de cuadros (plaid) con 8 colores.
  - **Área central izquierda:** Un cuadrado más grande de patrón de cuadros con 16 colores.
  - **Área inferior izquierda:** Un rectángulo grande con un patrón de cuadros (plaid) de 64 colores.
  - **Texto:** Superpuesto a la derecha de los patrones, en texto blanco, que describe las pruebas: "3BPL 8 COLORS", "4BPL 16 COLORS", "5BPL 32 COLORS", "COLOR PAIRS", "EHB 64 COLORS", "v1.0.5655", "ALL 64X64 PAIRS", "2X2 A/B CHECKER".
- **Elemento cambiante:**
  - En la esquina inferior derecha, se muestra un valor numérico que cambia entre las imágenes:
    - Imagen 1: `FR=00000058`
    - Imagen 2: `FR=000000C0`
    - Imagen 3: `FR=00000138`
  - Este valor parece incrementarse en pasos de 64 (58 → 120 → 182, pero el valor mostrado en la imagen 2 es 0xC0 = 192, lo que sugiere un incremento de 64, no 62).

**Análisis de los elementos:**
- **Contornos de los objetos:** Los contornos de los patrones gráficos y el texto son limpios, sin halos ni artefactos visibles. No hay píxeles cortados ni restos de posiciones anteriores.
- **Fondo alrededor:** El fondo negro permanece uniforme y no muestra ninguna alteración o "tapado" por los elementos gráficos.
- **Coherencia:** No hay elementos superpuestos, recortes ni artefactos visibles. El contenido es consistente en todas las imágenes, excepto por el valor numérico en la esquina inferior derecha, que cambia de forma secuencial.

**Conclusión:** Las imágenes muestran una pantalla de prueba de colores con un patrón de cuadros y texto descriptivo. El único cambio observable es el valor numérico en la esquina inferior derecha, que aumenta progresivamente en cada imagen. No hay elementos defectuosos, parpadeos ni artefactos visibles en los contornos o el fondo.
