# EVALUACIÓN DE VISIÓN — ui_amiga_306_hud_crpg

<!-- generado por tools/analyze/vision-final.mjs; no editar a mano -->

- Fecha: 2026-10-19T12:56:45.331Z
- Modelo: `qwen3-vl:8b-instruct-q8_0`
- Frames: frame_000.png, frame_002.png, frame_004.png, frame_005.png, frame_007.png, frame_009.png (6)
- Secuencia: `out/run/ui_amiga_306_hud_crpg/A500_debug/sequence`

## Prompt (canónico, tools/analyze/vision-prompt.txt)

```text
Las imágenes adjuntas van EN ORDEN TEMPORAL (si hay más de una). Describe con precisión qué ves: todos los elementos gráficos que aparecen, dónde están situados y qué hacen. Indica cualquier elemento que cambie entre imágenes y cualquier cosa que parezca extraña, defectuosa o incoherente (posiciones raras, parpadeos, recortes, artefactos, elementos superpuestos o incompletos). Presta especial atención a los objetos que se mueven: examina su CONTORNO en cada imagen (¿está limpio el perfil del objeto, sin banda o halo rectangular alrededor, sin restos de su posición anterior, sin píxeles cortados o colgando en los bordes?) y el FONDO inmediatamente alrededor (¿queda igual que cuando el objeto no estaba encima, o el objeto lo tapa con un rectángulo de color uniforme?). No adivines el propósito del programa: describe solo lo observable. Responde de forma concisa y ordenada.
```

## Respuesta (cruda)

**Imagen 1:**
- **Fondo:** Negro.
- **Elementos gráficos fijos:**
  - En la parte superior derecha: dos barras horizontales azules, una encima de la otra.
  - En el centro: tres rectángulos oscuros (azul oscuro o púrpura) dispuestos en forma de "T" invertida (uno vertical en el centro, uno horizontal a la izquierda, y otro horizontal más arriba y centrado).
  - En la parte inferior: una barra oscura horizontal que ocupa casi toda la anchura.
- **Elementos gráficos en la barra inferior (HUD):**
  - A la izquierda: cuatro iconos rojos (dos cuadrados, un círculo, un cuadrado con borde).
  - En el centro: una barra de texto con los verbos "LOOK", "TAKE", "USE", "OPEN", "TALK". "LOOK" está resaltado en amarillo. "TALK" está resaltado en rojo.
  - Debajo: "V:0 S:--" (V:0, S:--).
  - Debajo: "VERB:LOOK ITEM:--".
  - En la parte inferior: "HUD CRPG v1.0.5134 F__00200 FPS 43.90".
- **Estado:** El jugador está en modo "LOOK".

**Imagen 2:**
- **Fondo y elementos gráficos fijos:** Iguales a la imagen 1.
- **Cambios en la barra inferior (HUD):**
  - Los iconos a la izquierda han cambiado: ahora hay un cuadrado blanco y un cuadrado blanco con borde.
  - Los verbos: "TAKE" y "OPEN" están resaltados en rojo. "LOOK" y "USE" y "TALK" están en amarillo.
  - "V:3 S:--" (V:3, S:--).
  - "VERB:TAKE ITEM:--".
  - En la parte inferior: "HUD CRPG v1.0.5134 F__00200 FPS 45.0".
- **Estado:** El jugador ha cambiado a modo "TAKE".

**Imagen 3:**
- **Fondo y elementos gráficos fijos:** Iguales a la imagen 1.
- **Cambios en la barra inferior (HUD):**
  - Los iconos a la izquierda: igual que en la imagen 2.
  - Los verbos: "USE" y "OPEN" están resaltados en rojo. "LOOK", "TAKE" y "TALK" están en amarillo.
  - "V:3 S:--" (V:3, S:--).
  - "VERB:TAKE ITEM:--".
  - En la parte inferior: "HUD CRPG v1.0.5134 F__00200 FPS 45.0".
- **Estado:** El jugador ha cambiado a modo "USE".

**Observaciones:**
- **Cambio de estado:** Las imágenes muestran una secuencia temporal de cambio de verbo en el HUD: de "LOOK" a "TAKE" a "USE".
- **Elementos fijos:** Todos los elementos gráficos fijos (barras, rectángulos) permanecen en la misma posición y apariencia en todas las imágenes.
- **Resaltado:** El resaltado de los verbos cambia de color (amarillo a rojo) y posición según el verbo seleccionado.
- **Iconos:** Los iconos a la izquierda cambian de rojos a blancos en la imagen 2.
- **FPS:** El valor de FPS aumenta de 43.90 a 45.0.
- **Contorno y fondo:** No hay artefactos, bandas, halos ni píxeles cortados. Los objetos tienen contornos limpios y el fondo inmediato alrededor es uniforme y negro. No hay elementos superpuestos ni recortes.
