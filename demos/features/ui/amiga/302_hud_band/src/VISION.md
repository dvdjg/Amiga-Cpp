# EVALUACIÓN DE VISIÓN — ui_amiga_302_hud_band

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-18T12:04:49.268Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000_f0010.png, frame_004_f0014.png, frame_008_f0018.png, frame_011_f0021.png, frame_015_f0025.png, frame_019_f0029.png (6)
- Secuencia: `out/run/ui_amiga_302_hud_band/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Análisis de las imágenes en orden temporal:**

**Imagen 1:**
- **Fondo:** Negro.
- **Elementos gráficos:**
  - En la parte inferior, una barra de interfaz de usuario (HUD) con borde blanco.
  - Dentro de la barra HUD:
    - A la izquierda: Etiquetas "SCORE" y "ENERGY". Debajo de "ENERGY", una barra de progreso amarilla con una pequeña sección negra a la derecha.
    - A la derecha: Etiqueta "LIVES" y cuatro cuadrados amarillos.
    - En la parte inferior de la barra: texto "HUD BAND v1.0.4702 F 00000 FPS 00.0".
  - En la parte superior izquierda: Un objeto compuesto de dos bloques azules oscuros y un bloque azul claro más pequeño, que parece estar en movimiento hacia la derecha.
  - En la parte superior derecha: Dos bloques azules oscuros, uno horizontal y otro más pequeño y cuadrado.
- **Estado:** El objeto en movimiento está parcialmente sobre el bloque azul claro de su base. La barra de energía está casi llena. El score no es visible.

**Imagen 2:**
- **Fondo:** Negro.
- **Cambios respecto a la imagen 1:**
  - El objeto en movimiento ha avanzado hacia la derecha, ahora está completamente sobre el bloque azul claro de su base.
  - El texto "SCORE" ha aparecido, mostrando el valor "000001".
  - El resto de la interfaz (barra de energía, vidas, texto de versión) permanece igual.

**Imagen 3:**
- **Fondo:** Negro.
- **Cambios respecto a la imagen 2:**
  - El objeto en movimiento ha avanzado aún más hacia la derecha, ahora está completamente sobre el bloque azul oscuro de su base.
  - El texto "SCORE" sigue mostrando "000001".
  - El resto de la interfaz permanece igual.

**Observaciones:**
- El objeto en movimiento tiene un contorno limpio, sin halos ni artefactos visibles.
- El fondo permanece negro y uniforme en todas las imágenes.
- No hay elementos superpuestos ni recortes visibles.
- El objeto se mueve de izquierda a derecha, avanzando sobre los bloques de su base.
- El score se activa en la imagen 2 y permanece constante en la imagen 3.
- La barra de energía y las vidas no cambian entre las imágenes.
