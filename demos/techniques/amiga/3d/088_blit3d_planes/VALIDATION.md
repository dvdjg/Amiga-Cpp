# VALIDATION — 088_blit3d_planes

## Alcance y estado

El clipping y el relleno por plano están validados para el cubo de seis caras de esta demo, las trece poses estáticas O1 enumeradas, cuatro esquinas comparadas con CPU y una pose animada congelada. HOST-481 también recorre con CPU un pentágono escalado por los cuatro bordes y un tetraedro irregular parcialmente recortado. El contrato pixelario acepta ±2 px por la convención ONEDOT/EOR + FILL_XOR; esto no prueba exactitud píxel a píxel ni una garantía para cualquier polígono, escala o malla. La trayectoria ya se ve y cambia de frame en los 24 samples, pero la medida de FPS muestra cadencia inestable, por lo que la demo sigue ABIERTA para cierre de rendimiento.

`clip_camera_polygon` también cubre `near` y los cuatro planos laterales. El frustum culling por normal descarta caras traseras; el clipping conserva la parte visible de las caras delanteras. HOST-481 cubre el pentágono en ocho combinaciones de escala/traslación de borde y un tetraedro parcial con raster CPU, además de casos de triángulo, pentágono interior y cubo; sigue sin ser una prueba universal de geometría arbitraria ni de todas las escalas y mallas.

## Matriz estática O1

Cada build usa `K_CLIP_ORIENTATION=1`, `K_CLIP_BUFFERS=1` y `K_CLIP_REPEAT=0`. Los PNG `*_tilt_active.png` son recortes de 640×464, a escala 2× de la ventana lógica de 320×232. Por tanto, dos píxeles del recorte equivalen a un píxel del viewport.

| Caso | Pose | Evidencia del recorte O1 actual |
|---:|---|---|
| 0 | centro | Caras azul, roja y magenta visibles; silueta dentro del viewport. |
| 1 | derecha | Azul y rojo; `maxX=639`, llega a la última columna capturada. |
| 2 | izquierda | Rojo y magenta; `minX=2`, a un píxel lógico del borde. |
| 3 | arriba | Azul y magenta; `minY=4`, a dos píxeles lógicos del borde superior, dentro de la tolerancia pero no una prueba de contacto exacto. |
| 4 | abajo | Rojo, azul y magenta; `maxY=463`, última fila del viewport. |
| 5 | superior derecha | Azul; `minY=4`, `maxX=637`, ambos límites dentro de ±2 px. |
| 6 | superior izquierda | Magenta; `minX=2`, `minY=4`, ambos límites dentro de ±2 px. |
| 7 | inferior derecha | Rojo y azul; `maxY=463`, `maxX=637`, límites inferior/derecho dentro de ±2 px. |
| 8 | inferior izquierda | Rojo y magenta; `minX=2`, `maxY=463`, límites izquierdo/inferior dentro de ±2 px. |
| 9–12 | completamente fuera | Cero píxeles azules, rojos, magentas o blancos en el área visible para las cuatro direcciones. |

El analizador de píxeles confirma los límites y los conteos anteriores. Qwen3-VL describió limpias las cuatro esquinas; en CASE 3 detectó un margen negro superior pequeño, coherente con `minY=4`. CASE 9 fue descrito como negro y los cuatro casos exteriores dieron cero píxeles de geometría. Las respuestas visuales son evidencia auxiliar: el conteo por píxel fija las distancias al borde.

## Sonda CPU/Blitter y corrección del sentido de arista

- **HOST-481** pasa con `float` y `Fixed<s16,0>`: planos de cámara, capacidad del buffer, invariantes de proyección, culling, n-gon y poses de cubo. Incluye un caso que recorta la misma arista fija en los dos sentidos; antes de la normalización, los puntos diferían. `cut_edge` ahora coloca el extremo interior primero, haciendo el corte independiente del winding.
- **HOST-479** encontró otra asimetría en el clip 2D del rectángulo seguro: dos caras que comparten una arista cruzando `x=1` producían cortes `(1,0)` y `(1,1)` al recorrerla con winding opuesto. `clip_face_to_rect` normaliza ahora la interpolación dentro→fuera y la regresión compara las coordenadas y el raster CPU de las dos caras frente a su unión; ambos coinciden bit a bit.
- **L1-003** pasa con `detail=0x1ff`: bordes seguros, fila 0, residuo, cobertura CPU y juntas de relleno entre planos.
- **L1-005** pasa con `detail=0x3f`: las cuatro esquinas conservan cobertura CPU/Blitter dentro de ±2 px, HUD limpio y seis caras visibles.
- **L1-005 `--edge-stress`** congela la pose de la secuencia f0473 (`rotate=(1641,395,2375)`, traslación `(304,288)`) y compara CPU/Blitter en la ROI inferior derecha. Pasa: máscara de color CPU `0xC`, misma máscara Blitter, diferencia por plano dentro de ±2 px, cero excepciones de borde y 644 diferencias exactas de bitplane dentro de la tolerancia espacial. La inspección ampliada post-fix ya no muestra la línea azul discontinua interna; quedan dos píxeles azules en la columna derecha de f0480 y dos magentas en esa columna de f0518, ambos en el límite del viewport.
- **L1-005 `--edge-stress-cpu`** muestra en pantalla el buffer CPU de la misma pose para una comparación visual A/B; el veredicto de cobertura se calcula antes de copiar el oracle a la página visible.

La equivalencia CPU/Blitter es aproximada según el contrato ±2 px, no una igualdad bit a bit. Los píxeles de color que aparecen o desaparecen en el límite exterior pueden ser diferencias de rasterizado permitidas; no justifican afirmar que cualquier junta interna esté libre de coste visual sin inspección del caso concreto.

## Secuencia dinámica y visión

Comando de captura:

```bash
DEMO_FLAGS="-DK_CLIP_ANIMATE=1 -DK_CLIP_REPEAT=0 -DK_CLIP_BUFFERS=2" bash tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug --clean
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --wait-ms 3000 --settle-ms 1000 --sequence-frames 24 --sequence-interval-ms 500 --screenshot out/tmp/088_dynamic_fixed_full.png
node out/tmp/frames-differ.mjs out/run/088_blit3d_planes/A500_debug/sequence
node out/tmp/analyze-dynamic-clip.mjs out/run/088_blit3d_planes/A500_debug/sequence
```

El run-status avanzó por los samples desde f0029 hasta f0518; `frames-differ.mjs` encontró 23/23 pares consecutivos distintos. `analyze-dynamic-clip.mjs` muestra las caras de color conectadas durante el giro y, tras la corrección de `cut_edge`, ya no registra la cadena azul a lo largo de la junta roja/magenta del frame congelado f0473. Aún señala componentes de 1–2 píxeles en contornos oblicuos: el crop de f0369 los sitúa en la silueta magenta y Ollama los atribuye a su borde; f0480 y f0518 tienen dos muestras en la columna derecha, dentro del límite de ±2 px. El barrido de la banda HUD encontró cero píxeles rojos/azules en las 24 capturas; la inspección separada no acredita el solapamiento de magenta del objeto con cada glifo.

### Flicker en frames consecutivos

Comandos de la muestra temporal de ocho updates:

```bash
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --settle-ms 1000 --sequence-step-frames 8 --screenshot out/tmp/088_dynamic_step_full.png
node tools/vision-review/flicker-check.mjs --demo demos/techniques/amiga/3d/088_blit3d_planes --frames 8
```

La secuencia f0030–f0037 cambia en los 7/7 pares; `temporal-detect` marca cero bloques anómalos y cero candidatos. El `frame-diff` registra 1.320–1.896 píxeles cambiados por par y hasta 18 bloques de SSIM bajo mientras se mueve la geometría. Qwen3-VL responde «Anomalía: no; Tipo: ninguno; Zona relativa: abajo-derecha; Confianza: alta». Esto es evidencia de una ventana corta del recorrido, no una certificación de flicker para todo el ciclo; no se declara `flicker-baseline.json` con estos datos.

### Pase A — inventario sin contexto

Prompt exacto:

```text
Vas a analizar 9 capturas consecutivas muestreadas de una animación. No conoces el programa ni lo que pretende mostrar; describe SOLO lo que observas. Enumera los elementos distintos (forma, color, tamaño relativo), su posición relativa, cómo cambian entre imágenes y si algo parece aparecer/desaparecer, parpadear, cortarse o dejar rastros. Examina los contornos y el fondo inmediato; señala anomalías concretas por imagen. Responde en español.
```

Respuesta cruda de `qwen3-vl:8b-instruct-q8_0`:

```text
Analizando las nueve imágenes consecutivas en orden temporal, se observa una animación de objetos geométricos tridimensionales, posiblemente planos o caras de un modelo, que se mueven y transforman en un fondo negro. Las características clave son:

**Elementos visibles:**

* **Formas geométricas:** Las figuras son polígonos, principalmente cuadriláteros y triángulos, que se muestran en perspectiva isométrica o similar, sugiriendo un espacio 3D.
* **Colores:** Se utilizan principalmente tres colores distintos: rojo, azul y magenta (un tono púrpura vibrante). Estos colores se combinan en las figuras, creando distintas combinaciones visuales.
* **Fondo:** El fondo es completamente negro, lo que hace que las figuras destaquen con intensidad.
* **Texto informativo:** En la parte inferior de cada imagen aparece texto en color magenta que parece ser información de estado o de la aplicación, incluyendo "088 BLIT3D PLANES v1.0.5615", "PATH", "B2", "AMR1" y un identificador de frame que cambia en cada imagen (F0029, F0099, etc.).

**Cambio y movimiento entre imágenes:**

1. **Imagen 1 (F0029):** Se muestra una figura compuesta por dos rectángulos: uno azul en la parte inferior y otro rojo en la parte superior, ambos con bordes agudos que forman una especie de "L" o una estructura angular. El azul es más ancho y se extiende hacia la derecha, mientras que el rojo es más estrecho y se extiende hacia la izquierda.
2. **Imagen 2 (F0099):** La figura cambia drásticamente. Ahora es un solo rectángulo rojo, más grande y ubicado en la esquina superior izquierda. El azul y el magenta de la imagen anterior han desaparecido.
3. **Imagen 3 (F0168):** El rectángulo rojo se ha reducido en tamaño y se ha movido hacia la izquierda, casi en la esquina inferior izquierda. Aparece un triángulo azul de gran tamaño en la parte inferior, que parece estar girando o inclinándose. El borde del rectángulo rojo se superpone parcialmente con el triángulo azul.
4. **Imagen 4 (F0236):** La figura se vuelve completamente azul, más grande y ahora se encuentra en la esquina inferior derecha. El rectángulo rojo ha desaparecido completamente.
5. **Imagen 5 (F0310):** Ahora hay dos figuras distintas. Una figura grande y triangular, con bordes rojos en la parte superior y azules en la parte inferior, se extiende desde la esquina inferior izquierda. A la derecha, aparece una figura más pequeña, también triangular, con bordes rojos y azules, que se encuentra más cerca del centro.
6. **Imagen 6 (F0369):** Las dos figuras se han vuelto más grandes y se han movido hacia la derecha. La figura de la izquierda tiene bordes rojos y azules, mientras que la figura de la derecha es más ancha y también presenta bordes rojos y azules, pero con un tono magenta más claro.
7. **Imagen 7 (F0426):** Las figuras se han vuelto más grandes y se han movido aún más hacia la derecha. Ahora hay dos figuras triangulares superpuestas, una en la parte inferior izquierda y otra en la parte inferior derecha. Ambas tienen bordes rojos y azules.
8. **Imagen 8 (F0480):** La figura de la izquierda ha desaparecido. Solo queda una figura grande y triangular, con bordes rojos y azules, en la esquina inferior derecha.
9. **Imagen 9 (F0518):** La figura se ha vuelto más pequeña y se ha movido hacia arriba. Ahora es una figura triangular más pequeña, con bordes rojos y azules, ubicada en la parte inferior derecha. La figura que estaba en la izquierda ha desaparecido completamente.

**Anomalías y efectos:**

* **Aparecen y desaparecen figuras:** Entre las imágenes, figuras completas aparecen y desaparecen, y otras cambian de forma y color.
* **Superposición:** Las figuras a menudo se superponen, creando zonas de color mezclado (como el magenta en la imagen 3).
* **Movimiento de posición:** Las figuras cambian constantemente de posición, desplazándose principalmente hacia la derecha y hacia abajo.
* **Cambio de color:** El color de las figuras cambia de forma y tonalidad a lo largo del tiempo, con un predominio del rojo, el azul y el magenta.
* **Cambios en tamaño:** Las figuras varían en tamaño, a veces creciendo y a veces reduciéndose.
* **Contornos nítidos:** Las figuras tienen contornos muy definidos, lo que sugiere que se trata de una animación digital en un entorno gráfico, posiblemente con un motor de renderizado 3D simple. La línea de separación entre colores es muy nítida y parece que no hay efectos de sombreado o degradado.

En resumen, la animación muestra una secuencia de transformaciones y movimientos de figuras geométricas en 3D, con colores primarios y secundarios, que cambian de forma, tamaño y posición de manera continua, creando una sensación de dinamismo y evolución visual.
```

### Pase B — seguimiento y defectos

Prompt exacto:

```text
Céntrate en el sólido geométrico coloreado. Sigue su evolución en orden: posición relativa, orientación, forma visible y colores. ¿Su movimiento y rotación parecen continuos o hay saltos/paradas? ¿El contorno cambia limpiamente cuando llega a los bordes y esquinas? ¿Detectas huecos, bandas, píxeles aislados o restos de la posición anterior?
```

Respuesta cruda de `qwen3-vl:8b-instruct-q8_0`:

```text
Análisis de la evolución del sólido geométrico coloreado en las imágenes proporcionadas.

1. Movimiento y Rotación: Continuidad o Saltos?

El movimiento y la rotación del objeto parecen altamente continuos. No hay saltos ni paradas bruscas en su trayectoria.

* Trayectoria: El objeto se mueve de manera fluida de la parte superior derecha de la pantalla hacia la izquierda y luego hacia abajo, creando una curva suave. En la imagen 4, el objeto está en la parte derecha, en la imagen 5 se ha movido hacia la izquierda, en la imagen 6 se ha inclinado más hacia abajo, y en la imagen 7 se ha desplazado aún más hacia abajo y a la izquierda.
* Rotación: La rotación del objeto también es continua. Comienza en la imagen 1 con una orientación que parece estar en 3/4 de vuelta (como si estuviera girando hacia la izquierda desde una posición superior), y en la imagen 2 ya está completamente girado hacia la izquierda (posición "flecha" apuntando a la izquierda). En la imagen 3, se ha inclinado hacia abajo. Luego, en la imagen 4, vuelve a girar hacia la derecha, y en la imagen 5, el objeto ya está girado hacia la derecha con una inclinación hacia abajo. Este giro es suave y constante, sin saltos.

2. Contornos y Transiciones:

Los contornos del objeto cambian limpiamente cuando llega a los bordes y esquinas.

* Los bordes del objeto siempre están bien definidos y no se ven borrosos.
* Las esquinas del objeto, cuando llegan al borde de la pantalla o se encuentran con otros planos, se ven nítidas y no hay "efectos de bordes" o "desdibujado" que indiquen una transición imperfecta.
* El cambio de color entre los diferentes planos (rojo, azul, magenta) siempre ocurre en líneas rectas y definidas.

3. Huecos, Bandas o Restos de Posición Anterior:

No se detectan huecos, bandas, píxeles aislados o restos de la posición anterior.

* Huecos: En ninguna imagen se aprecia un hueco o área sin contenido en el objeto.
* Bandas: No se observan bandas horizontales o verticales que sugieran una superposición o un efecto de renderizado incompleto.
* Píxeles aislados o restos: No hay píxeles sueltos ni restos de la posición anterior.

Conclusión: El objeto en movimiento y rotación es suave, continuo y perfectamente renderizado. No hay saltos ni irregularidades visibles.
```

### Pase C — contraste con la intención

Prompt exacto:

```text
La intención de este modo es mostrar un cubo convexo de seis caras que rota y recorre los cuatro bordes de la ventana; las caras visibles se componen con azul, rojo y magenta sobre negro, y la franja HUD debe permanecer legible. Para cada imagen indica si el cubo está presente o parcialmente fuera de la ventana, si su silueta y colores son coherentes y si la franja queda libre de geometría. ¿Hay saltos, huecos, restos, bandas o píxeles sueltos? Distingue lo que ves con claridad de lo ambiguo.
```

Respuesta cruda de `qwen3-vl:8b-instruct-q8_0`:

```text
Analizando cada imagen según los criterios establecidos, aquí está mi evaluación detallada:

---

**Imagen 1 (f0029):**
- **Cubo:** Presente y completamente dentro de la ventana. La silueta es clara: un cubo con caras roja, azul y magenta (la magenta es visible como una franja en la parte inferior derecha). Las transiciones entre colores son nítidas y coherentes.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 2 (f0099):**
- **Cubo:** Presente y completamente dentro de la ventana. Es un plano rojo con bordes afilados. La silueta es coherente con una cara del cubo en perspectiva.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 3 (f0168):**
- **Cubo:** Presente y completamente dentro de la ventana. Se observa una cara roja y una cara magenta, con un borde azul entre ellas. La silueta es coherente con la rotación del cubo.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 4 (f0236):**
- **Cubo:** Presente y completamente dentro de la ventana. La silueta es clara: una cara azul con una franja roja en la parte superior. Las transiciones entre colores son nítidas y coherentes.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 5 (f0310):**
- **Cubo:** Presente y completamente dentro de la ventana. Se observan dos caras del cubo: una roja en la parte superior izquierda y una magenta en la parte inferior. Las transiciones entre colores son nítidas y coherentes.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 6 (f0369):**
- **Cubo:** Presente y completamente dentro de la ventana. La silueta es clara: una cara magenta con una franja roja en la parte superior. Las transiciones entre colores son nítidas y coherentes.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 7 (f0426):**
- **Cubo:** Presente y completamente dentro de la ventana. La silueta es clara: una cara azul con una franja roja en la parte superior. Las transiciones entre colores son nítidas y coherentes.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 8 (f0480):**
- **Cubo:** Presente y completamente dentro de la ventana. La silueta es clara: una cara roja con una franja azul en la parte inferior. Las transiciones entre colores son nítidas y coherentes.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Imagen 9 (f0518):**
- **Cubo:** Presente y completamente dentro de la ventana. La silueta es clara: una cara azul con una franja roja en la parte superior. Las transiciones entre colores son nítidas y coherentes.
- **HUD:** Libre de geometría. El texto en púrpura está completamente legible y no se superpone con el cubo.
- **Conclusiones:** Perfectamente visible, sin saltos ni huecos. *Claro.*

---

**Resumen Final:**

- **Cubo presente y completamente dentro de la ventana en todas las imágenes.**
- **Silueta y colores coherentes en todas las imágenes.**
- **HUD libre de geometría en todas las imágenes.**
- **No hay saltos, huecos, restos, bandas ni píxeles sueltos en ninguna imagen.**
- **Todo lo que se ve es claro y definitivo.**

No hay ninguna imagen que muestre problemas de visualización según los criterios especificados.
```

La respuesta C afirma erróneamente que el cubo está completamente dentro en todas las imágenes: los límites de color y los casos estáticos demuestran cruces por los bordes. Por esa contradicción, las afirmaciones espaciales del modelo no se toman como evidencia de clipping; la matriz de píxeles y la sonda CPU/Blitter determinan el resultado. La evaluación final sin pistas está en [`src/VISION.md`](src/VISION.md).

## Rendimiento y cadencia

Dos medidas post-fix con `node tools/debug/measure-fps.mjs 088_blit3d_planes A500_debug --json --duration-ms 20000` dan 31,91 fps/222.296 ciclos por frame y 31,97 fps/221.898 ciclos por frame (≈1,56 fields/frame). Las ocho ventanas de la primera medida oscilan entre 27,73 y 36,16 fps, así que la cadencia es inestable. La media supera el suelo de 25 fps permitido para relleno de polígonos, pero no es una secuencia uniforme ni alcanza un frame por VBlank PAL; por ello esta demo no se declara cerrada. La fila trazable está en [`BITACORA_SCROLL_TILES.md`](../../../../../docs/guides/roadmap/BITACORA_SCROLL_TILES.md).

Perfil de ocho fields con `node tools/debug/winuae-profile.mjs 088_blit3d_planes A500_debug 8 --settle-ms 12000`: CPU ocupada 142.102 ciclos/field, 0 libres; `profile-samples.mjs` atribuye 25,2 % a `clip_camera_polygon`, 21,6 % a `clip_face_to_rect`, 14,8 % a `fill_polygons_by_plane_patterns`, 9,9 % a `memset` y 9,2 % a `wait_blitter`. La atribución de rutinas aún no es fiable: también asigna 3,9 % a `AmigaBackend::configure_memory` en cada field, aunque 088 solo la llama en `init` (`src/main.cpp:196`). No se elige una optimización a partir de esa tabla hasta verificar la relocación PC→símbolo del perfil contra callsites y confirmar que la captura comienza después de READY. La campaña P5 sigue abierta; después de corregir el medidor, medir una variable, comprobar la salida CPU y repetir las ventanas de cadencia.

### Experimento A — eliminar clear de scratch en `clip_face_to_rect`

**Hipótesis previa:** `clip_face_to_rect` escribe todos los elementos que lee: copia `xs/ys[0..n)` en A y cada pasada de Sutherland–Hodgman escribe el prefijo `0..out_n` del buffer destino antes del intercambio. Los cuatro arrays locales no necesitan zero-init; quitarlo debe eliminar trabajo de `memset` sin cambiar píxeles. El perfil de ocho frames atribuyó 12,0 % a `memset` (136.476 ciclos acumulados) y 11,9 % a `clip_face_to_rect`.

**Variable única:** retirar `{}` de `ax`, `ay`, `bx` y `by` en `clip_face_to_rect`. **Baseline:** 32,01 fps, 221.583 ciclos/frame, ventanas de 27,52–36,45 fps, 1,562 campos/frame y 0 ciclos CPU libres. **Delta esperado:** reducción de al menos 6.650 ciclos/frame (3 %); **falsación:** mejora inferior a 3 % o cualquier divergencia de cobertura, color, HUD o secuencia. Revertir restaurando los cuatro inicializadores. La secuencia de ocho updates y la comparación CPU/Blitter son obligatorias antes de conservar el cambio.

**Resultado y decisión:** la build experimental dio 32,38 fps y 219.101 ciclos/frame (−1,12 % de ciclos, +1,16 % de fps), bajo el umbral de 3 %; se restauraron los cuatro inicializadores. La hipótesis queda falsada para esta ruta y no se conserva el cambio. La distribución de ciclos del perfil se debe reexaminar antes de elegir el siguiente candidato.

### Experimento B — omitir el plano near tras comprobar el objeto

**Hipótesis previa:** los ocho vértices transformados de este cubo están delante de `near_z` en toda la trayectoria; cuando se comprueba esa condición en cada update, ni el test global ni cada cara necesitan volver a calcular la distancia al plano near. El recorte lateral conserva el mismo orden y las mismas intersecciones. La cota se verifica con los vértices reales del frame, no se asume por el nombre de la malla.

**Variable única:** excluir el plano near del test/clip solo si los ocho vértices cumplen `z >= near_z`; el caso que no cumple conserva los cinco planos. **Baseline:** 32,01 fps y 221.583 ciclos/frame, CPU con 0 ciclos libres; `clip_camera_polygon` concentra 42,4 % de las muestras. **Delta esperado:** ≥6.650 ciclos/frame (3 %), al quitar una evaluación de plano en cada test de vértice/cara; **falsación:** mejora inferior al 3 %, diferencia CPU/Blitter por encima de ±2 px, o cambio de geometría/HUD/flicker. Comparar byte a byte los resultados de ambas variantes para polígonos enteros delante de `near` y validar una secuencia de ocho updates con Ollama.

**Resultado y decisión:** HOST-481 confirmó igualdad exacta de salida para polígonos frontales en `float` y `Fixed`. La medida A de 20 s dio 31,91 fps/222.296 ciclos/frame (ventanas 28,13–36,05); B dio 32,69 fps/217.017 ciclos/frame (28,30–36,45), una mejora de 2,45 % en fps y 2,37 % menos ciclos, inferior al umbral preacordado. No se conserva el atajo near; el test API y la selección condicional de la demo se retiraron. La cadencia sigue inestable.

### Experimento C — fast path para caras estrictamente interiores

**Hipótesis previa:** cuando todos los vértices de una cara quedan estrictamente dentro de los cinco planos y no fueron saturados, la pasada final de `clip_camera_polygon` no necesita volver a acotar cada coordenada con dos `mul_div` por vértice; basta copiar el prefijo ya saneado. La comparación estricta evita cambiar los casos que tocan un plano y el test de capacidad sigue precediendo a cualquier escritura.

**Variable única:** devolver los vértices de entrada saneados tras verificar que todos siguen estrictamente dentro, omitiendo solo el ajuste final de bounds para esa cara. **Baseline:** 31,91 fps, 222.296 ciclos/frame y 0 ciclos libres; `clip_camera_polygon` concentra 42,4 % de las muestras. **Delta esperado:** ≥6.650 ciclos/frame (3 %), al omitir dos `mul_div` por vértice en caras interiores; **falsación:** menos de 3 % de mejora, cualquier cambio en el resultado HOST-481, CPU/Blitter fuera de ±2 px o anomalía visual. Revertir el fast path y conservar la pasada exacta si falla. La validación de render exige secuencia de ocho updates con análisis de Ollama.

### Experimento D — proyectar y recortar en pantalla para esta malla

**Hipótesis previa:** el backend ya recorta cada cara proyectada al rectángulo seguro antes de rasterizar. En el cubo de 088, si todos los vértices están delante de `near_z`, proyectar sus ocho vértices una vez y delegar los bordes laterales al clip 2D puede sustituir cinco planos de cámara por cuatro planos 2D por cara. Si algún vértice no es proyectable o cruza near, se mantiene el camino 3D actual.

**Variable única:** ruta condicional `K_CLIP_PROJECT_BEFORE_SCREEN_CLIP`, inicialmente desactivada; con ella activada, se reutilizan ocho proyecciones y el backend recorta las caras al rectángulo seguro. **Baseline:** 31,91 fps y 222.296 ciclos/frame; 42,4 % de muestras en `clip_camera_polygon`. **Delta esperado:** ≥11.115 ciclos/frame (5 %), retirando el clipping 3D lateral de caras frontales; **falsación:** menos de 5 % de mejora, cualquier pose/color/HUD incoherente, diferencia CPU/Blitter fuera de ±2 px o anomalía en secuencia. Validar además HOST-481, L1-005, `--edge-stress` y visión Ollama A/B antes de conservarlo.

**Resultado:** con el mismo build O1, trayectoria, dos buffers y ventana de 20 s, A (`=0`) dio 31,91 fps/222.296 ciclos/frame y B (`=1`) 40,75 fps/174.101 ciclos/frame: +27,7 % de fps y −21,7 % de ciclos por frame (1,567→1,227 fields/frame). B sigue sin alcanzar un frame por field y el medidor la marca **INESTABLE**: ventanas 35,38–44,81 fps. El cambio se conserva como fast path geométricamente equivalente, pero la cadencia aún no cierra.

El perfil de B mantiene 142.102 ciclos CPU ocupados por field y 0 libres; muestras: `clip_camera_polygon` 21,7 %, `clip_face_to_rect` 20,9 %, `fill_polygons_by_plane_patterns` 17,1 %, `wait_blitter` 9,9 % y `memset` 9,0 %. La pasada host del render y la interpolación no se modifican; las medidas sitúan la siguiente oportunidad en el clip 2D seguro y el llenado por plano.

**Verificación visual/determinista:** la secuencia B de ocho frames consecutivos f0040–f0047 cambia 7/7 pares; `flicker-check.mjs` detecta cero anomalías OpenCV y Qwen3-VL responde «Anomalía: no; Tipo: ninguno». La evaluación de Ollama sobre f0042–f0049 describe movimiento continuo, caras coherentes y HUD legible, sin huecos ni puntos aislados. Una captura espaciada por 700 ms sugirió una línea punteada pequeña en dos fases; no reapareció en la secuencia consecutiva. La comparación por fase en los mismos frames reales f0044–f0049 da IoU 0,977–0,985 y MAD de color 0,0–0,6; las capturas abiertas muestran la misma silueta y HUD, con diferencias subpíxel/de borde. El alcance es ese tramo y esas poses, no toda la vuelta.

### Experimento E — fast path de caras dentro del rectángulo seguro

**Hipótesis previa:** `clip_face_to_rect` ejecuta cuatro pasadas Sutherland–Hodgman incluso cuando todos los vértices ya están dentro del rectángulo seguro. Un escaneo corto seguido de copia exacta debe evitar esas pasadas y la inicialización de los scratch para esas caras, manteniendo sin cambios el camino de clipping para los polígonos que tocan o cruzan un borde.

**Variable única:** devolver la cara sin cambios si todos sus vértices cumplen los cuatro límites inclusivos. **Baseline:** ruta D, 40,75 fps/174.101 ciclos/frame, ventanas 35,38–44,81 fps; perfil: `clip_face_to_rect` 20,9 % y 0 ciclos CPU libres por field. **Delta esperado:** ≥5.223 ciclos/frame (3 %); **falsación:** menos de 3 % de mejora o cualquier diferencia de coordenadas en HOST-479/CPU/Blitter, HUD o salida visual. La ruta que cruza bordes conserva la implementación de referencia.

**Resultado y decisión:** B+E dio 40,94 fps/173.262 ciclos/frame frente a B=40,75/174.101 (+0,47 % de fps, −0,48 % de ciclos), por debajo del 3 % preacordado. Se retiraron el fast path y su prueba provisional; no se conserva la optimización. El candidato D permanece y la siguiente medición debe elegir una variable con ahorro esperado mayor que el ruido del medidor.

### Experimento F — eliminar el fallback de cámara con una cota estática

**Hipótesis previa:** la ruta D todavía conserva un test dinámico y un fallback a `clip_camera_polygon`. Para el cubo de medio lado 150, el componente Z de cualquier vértice tras una rotación está acotado conservadoramente por `3×150`; con `kZ0=900` y un margen de redondeo 16, el mínimo sigue muy por delante de `near_z=64`. Una aserción de compilación permite proyectar directamente sin evaluar ni ejecutar el clipping de cámara, manteniendo el recorte 2D seguro del backend.

**Variable única:** sustituir la prueba/fallback por la cota estática `kZ0 - 3*kHalf - 16 > near_z`. **Baseline:** ruta D, 40,75 fps/174.101 ciclos/frame y `clip_camera_polygon` 21,7 % de muestras. **Delta esperado:** ≥8.705 ciclos/frame (5 %), al eliminar el fallback de cámara y las ocho comprobaciones dinámicas; **falsación:** menos de 5 % de mejora, cualquier cambio superior a ±2 px, borde/HUD fuera de sus regiones o anomalía en la secuencia completa. HOST-481 conserva el caso general con clipping near; esta optimización solo afecta el cubo acotado de 088.

**Resultado y decisión:** F dio 40,75 fps/174.062 ciclos/frame frente a D=40,75/174.101 (−0,02 % de ciclos), muy por debajo del 5 % esperado. Las muestras ya no atribuyeron tiempo a `clip_camera_polygon`, pero el total no se movió; se restaura el fallback con comprobación de `near` y no se conserva el cambio F.

### Experimento G — espaciar la actualización del contador HUD

**Hipótesis previa:** el perfil F atribuye 27,5 % de muestras a `Surface::draw_text_words` y el HUD solo necesita un contador legible, no un valor nuevo por frame. Cada una de las dos páginas puede recordar su último bucket; al cambiar el bucket, el back buffer se actualiza cuando le toca, manteniendo ambas páginas sincronizadas sin escribir en la que está visible.

**Variable única:** refrescar el contador cada ocho frames por página (mostrar el múltiplo de ocho más reciente) en lugar de cada frame. **Baseline:** 40,75 fps/174.062 ciclos/frame, `draw_text_words` 27,5 % de muestras, cadencia inestable. **Delta esperado:** ≥8.703 ciclos/frame (5 %); **falsación:** menos de 5 % de mejora, páginas con contadores alternantes/ilegibles, cualquier artefacto geométrico o invasión del HUD. Revertir volviendo al intervalo 1. El gate de secuencia comprueba cambio de geometría, continuidad, cuatro bordes/esquinas y aislamiento de color del HUD.

**Resultado:** shift=0 midió 40,75 fps/174.101 ciclos/frame; shift=3 midió 43,00 fps/164.969 ciclos/frame (+5,5 % de fps, −5,25 % de ciclos). El perfil `draw_text_words` bajó de 27,5 % a 0,4 % de muestras; se conserva el intervalo de ocho frames con caché independiente por página. La cadencia sigue **INESTABLE** (38,52–47,54 fps), aún bajo el objetivo de un frame por VBlank y sin liberar ciclos CPU por field.

### Experimento H — saltar pasadas 2D cuyos vértices ya están dentro

**Hipótesis previa:** `clip_face_to_rect` repasa las aristas por cada uno de cuatro semiplanos aunque una cara parcialmente recortada solo cruce uno de ellos. Un escaneo de límites sobre el polígono corriente puede omitir exactamente las pasadas con todos los vértices interiores; las caras que cruzan el plano mantienen la interpolación actual.

**Variable única:** antes de cada pasada Sutherland–Hodgman, comprobar si todos los vértices están en el lado interior y continuar sin copiar si lo están. **Baseline:** ruta G, 43,00 fps/164.969 ciclos/frame; perfil: `clip_face_to_rect` 20,1 % de muestras, `draw_text_words` 0,4 %. **Delta esperado:** ≥4.949 ciclos/frame (3 %); **falsación:** mejora inferior al 3 %, salida HOST-479 distinta, CPU/Blitter fuera de ±2 px o anomalía de borde/HUD. HOST-481, L1-003, L1-005 y la secuencia de 32 capturas son los gates del cambio.

**Resultado y decisión:** HOST-479 y L1-003 pasaron, pero WinUAE dejó de avanzar el contador de frames en dos medidas de 20 s (tras ~2,5 s de muestreo); la media cayó a 16,89 y 10,46 fps. Una medida corta dio 41,46 fps, por lo que el comportamiento es temporalmente inestable y no aceptable. Se retiró el salto de pasadas; la prueba CPU exacta de `clip_face_to_rect` se conserva en HOST-479. La causa del stall queda SIN VERIFICAR y el fast path no se conserva.

### Experimento I — omitir la contención global de cámara en la ruta proyectada

**Hipótesis previa:** en la ruta de pantalla, los ocho vértices ya se proyectan para formar las caras; `camera_contains_vertices` solo decide entre reutilizar esa proyección global o volver a recorrer cada cara. Desactivar ese test global debe ahorrar cinco distancias por vértice y no altera el resultado: cuando alguna cara toca un plano, el backend sigue aplicando el rectángulo seguro; el fallback de cámara permanece si un vértice no se proyecta por near.

**Variable única:** omitir `camera_contains_vertices` únicamente con `K_CLIP_PROJECT_BEFORE_SCREEN_CLIP=1`; el build A/B con valor 0 conserva la decisión original. **Baseline:** ruta G, 42,96 fps/165.132 ciclos/frame, ventanas 38,52–47,17 fps. **Delta esperado:** ≥4.954 ciclos/frame (3 %); **falsación:** mejora inferior al 3 %, cambio en HOST-481/CPU, clipping fuera del viewport o anomalía de secuencia/HUD. La secuencia completa comprueba que las cuatro esquinas continúan tocándose.

**Resultado y decisión:** la build B dio 44,14 fps/160.699 ciclos/frame frente al baseline documentado de 42,96/165.132 (+2,7 % de fps, −2,68 % de ciclos), por debajo del umbral del 3 %. La medida A con la combinación exacta de control no fue reproducible por el canal GDB (secuencia run-demo sí avanzó), así que la diferencia queda como indicio, no como delta concluyente. Se retira el atajo de contención global y se conserva la ruta D+G, cuya medida de 42,96 fps se obtuvo con una ventana GDB completa.

## Resultado acumulado de la ruta conservada D+G

Tras retirar F, H e I, el build conserva la ruta D de proyección/recorte 2D y la ruta G del HUD por buckets. La última ventana GDB completa antes de la regresión HOST-479 dio **42,98 fps/165.053 ciclos por frame**, frente al baseline original **31,91 fps/222.296 ciclos** (+34,7 % de fps, −25,7 % de ciclos). La re-medida del árbol actual con la corrección HOST-479 dio **43,60 fps/162.701 ciclos/frame** (1,147 fields/frame; `detail=0x3`, 883 frames), con ventanas **40,02–48,34 fps** y estado **INESTABLE**. Esta variación no alcanza 50 fps ni demuestra cadencia uniforme; el cierre de rendimiento sigue abierto.

Comandos reproducibles de aserción:

```bash
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --settle-ms 1000 --sequence-frames 32 --sequence-interval-ms 500
node tools/analyze/verify-088-sequence.mjs out/run/088_blit3d_planes/A500_debug/sequence --screen-x 76 --screen-y 36 --require-coverage
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --settle-ms 1000 --sequence-step-frames 8
node tools/analyze/verify-088-sequence.mjs out/run/088_blit3d_planes/A500_debug/sequence --screen-x 76 --screen-y 36 --consecutive
node tools/vision-review/flicker-check.mjs --demo demos/techniques/amiga/3d/088_blit3d_planes --frames 8
```

El barrido de 32 muestras pasó: cubre cuatro bordes y cuatro esquinas, incluye rojo/azul/magenta y cuenta cero píxeles rojos/azules en el HUD. La secuencia de ocho updates consecutivos tiene frames reales f0043–f0050, 7/7 pares con cambios y cero anomalías OpenCV; Qwen3-VL no detectó cortes, huecos ni residuos en los pases A/B. En el pase C el modelo afirmó que las imágenes eran idénticas, en contradicción con el diff por pares; esa afirmación se descarta porque `verify-088-sequence.mjs` midió cambios en los ocho frames.

Tras la queja de una costura oscura, HOST-479 añadió el caso mínimo de winding opuesto en el clip 2D: antes del fix el host fallaba con cortes distintos `(1,0)`/`(1,1)` y la máscara CPU de dos caras discrepaba de la unión; después, coordenadas y máscara coinciden bit a bit. En una validación nueva del build animado, `verify-088-sequence.mjs` pasó 32 muestras espaciadas 500 ms, con cobertura de los cuatro bordes/esquinas, los tres colores y cero píxeles rojos/azules en el HUD. La secuencia congelada f0046–f0053 cambió en los 7/7 pares; el HUD quedó limpio y `flicker-check.mjs` marcó cero anomalías OpenCV. El pase visual dirigido de Ollama no encontró huecos negros en las juntas de ese tramo; su evaluación canónica de la secuencia espaciada describió cambios de posición/color y no detectó halos, píxeles cortados ni residuos. En cambio, el pase canónico de los ocho frames consecutivos dijo erróneamente que eran idénticos, contradicho por el diff de 1.040–1.468 píxeles por par; para movimiento prevalece el diff y el modelo queda como apoyo visual. Esto valida el caso reproducido y el tramo inspeccionado, no una garantía para toda malla/pose arbitraria.

## Límites y comandos de regresión

El margen seguro horizontal del trazador sigue siendo `min_x=1`; no se añadió una guarda física de 16 px ni se modificaron stride y punteros BPL. El alcance visual aquí documentado no prueba un pentágono arbitrario que se traslada y escala, cualquier número de vértices, todos los ángulos posibles, ni igualdad exacta de cada pixel en todos los bordes.

```bash
bash tools/run-host-tests.sh tests/host/graphics/481_render3d_clip
bash tests/amiga/l1_backend/003_plane_fill_bounds/verify.sh
bash tests/amiga/l1_backend/005_render3d_corners/verify.sh
bash tests/amiga/l1_backend/005_render3d_corners/verify.sh --edge-stress
```
