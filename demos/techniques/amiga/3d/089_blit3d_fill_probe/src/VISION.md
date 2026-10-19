# EVALUACIÓN DE VISIÓN — 089_blit3d_fill_probe

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T19:23:22.937Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png (1)
- Secuencia: `out/run/089_blit3d_fill_probe/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

- **Fondo**: Negro uniforme.
- **Elementos gráficos**:
  - **Cuadrado azul**: Ubicado en la parte superior izquierda. Es un cuadrado sólido de color azul intenso, con bordes limpios y sin artefactos. No muestra movimiento ni cambios.
  - **Rombo rojo**: Ubicado en la parte superior derecha. Es un rombo sólido de color rojo intenso, orientado con una diagonal horizontal. Sus bordes son limpios, sin halos ni píxeles cortados. No muestra movimiento ni cambios.
  - **Texto azul**: Ubicado en la parte inferior central. Muestra el texto “G89 FILL PROBE” en letras mayúsculas de color azul. El texto es estático y no cambia.
- **Estado general**: Todos los elementos están fijos, sin movimiento, parpadeo ni cambios entre imágenes. No hay artefactos, superposiciones ni recortes visibles. El fondo negro permanece uniforme alrededor de todos los elementos.
