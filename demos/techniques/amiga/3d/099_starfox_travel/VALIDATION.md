# Validación de la demo 099 — Star Fox style rail travel

Estado: **VERIFICADA en el alcance probado** (WinUAE A500 release, secuencia de 8 frames, visión local, contrato de movimiento, flicker y regresión). El escenario es un traveling no jugable; no se valida entrada, colisión ni una A500 física.

## Comandos de la pasada

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel \
  --config A500_release --settle-ms 1200 --sequence-frames 8 --sequence-interval-ms 300
export AMIGA_WINUAE_PATH="$USERPROFILE/Documents/programa/AI/Amiga/WinUAE-DBG/bin"
node tools/debug/measure-fps.mjs 099_starfox_travel A500_release --json
node tools/analyze/verify-3d-sequence.mjs out/run/099_starfox_travel/A500_release/sequence \
  --min-frames 8 --require-motion --require-distinct --top 16 --bottom 208
node tools/analyze/vision-final.mjs 099_starfox_travel \
  out/run/099_starfox_travel/A500_release/sequence --frames 6
bash ./tools/test-regression.sh --demo demos/techniques/amiga/3d/099_starfox_travel
bash ./tools/test-regression.sh --flicker --require-flicker-ok \
  --demo demos/techniques/amiga/3d/099_starfox_travel
```

`measure-fps` debe usar el fork (`AMIGA_WINUAE_PATH=.../WinUAE-DBG/bin`); con el WinUAE stock de la extensión el side-channel no resolvió `g_eng_run_status` en esta sesión (runtime=0, sin READY).

## Medidas

| Medición | Resultado |
|---|---|
| **Final release A500** | **28,48 fps**, 249.041 ciclos/frame, **1,755 campos/frame**; ventanas 27,73–29,48; mínimo 27,3; 566 frames; `stable=true`; sin aviso de cuantización |
| Suelo de la demo | 25 fps (relleno poligonal); **cumple con margen** |
| `K099_ABL=1` (sin dibujo, sin HUD, sin clear) | 49,87 fps / 142.244 ciclos |
| `K099_ABL=2` (solo los 3 sólidos, sin clear/estrellas/balas/HUD) | 40,32 fps / 175.927 ciclos |
| `K099_ABL=3` (solo clear de los 2 planos) | 49,92 fps / 142.102 ciclos (cabe en el hueco de campo; no marca cadencia) |
| Desglose aproximado del frame final | 142k base + ~34k de los 3 sólidos + ~74k de clear/estrellas/balas/HUD/update |
| Secuencia de 8 frames | **7/7 cambios**, `maxEstatico=0`, 8 frames distintos (MD5 distintos) |
| Flicker | gate **OK** con `flicker-baseline.json`; informe con 36 candidatos y 55 bloques bajos máximos, todos en cazas/estrellas en movimiento; sin flicker/tearing en la revisión de frames |
| Regresión de demo | build/run/analyze **ok**; `Regression OK` |
| Tests host del escritor de spans | HOST-217/474/479/481/482 **OK** |
| Visión canónica final | `src/VISION.md` generado por `vision-final.mjs`; ve la nave y los cazas rojos moviéndose (con sobreconteo ocasional), contornos limpios y sin artefactos |

El aviso del analizador genérico `FAIL: green=0 < 1` es el overlay genérico que no aplica a esta demo (no hay analizador propio ni `vision-points.json`); la regresión lo registra como `analyze ok` y la validación visual se hace con la secuencia 3D y la visión.

## Validación visual con Ollama

Modelo: `qwen3-vl:8b-instruct-q8_0`, Ollama local. Secuencia: `out/run/099_starfox_travel/A500_release/sequence` (8 frames). Los prompts A/B/C son los de depuración; el prompt canónico sin pistas está en `src/VISION.md`.

### A — Inventario sin contexto (frame 0)

**Prompt:**

```text
Vas a analizar una captura de una animacion. No conoces el programa ni lo que pretende mostrar; describe SOLO lo que observas. 1) Inventario: enumera los elementos distintos que ves (forma, color, tamano relativo) y cuantos hay de cada tipo. 2) Dinamica: para cada elemento, cambia de posicion, tamano, forma o color? Como se relaciona con los demas (orden, solapes, capas)? 3) Alguna zona aparece cortada, con huecos negros, bandas horizontales, rectangulos de borrado o pixeles que no pertenecen al dibujo? Indica la zona relativa (arriba/centro/abajo, izquierda/centro/derecha). Responde en espanol, con observaciones concretas y sin inventar coordenadas en pixeles.
```

**Respuesta cruda (resumen literal):** ve un fondo negro con estrellas, un triángulo azul claro con un punto amarillo (nave), dos triángulos rojos (enemigos), tres rocas grises y una semiesfera azul en la banda inferior, bandas horizontales y los textos `099 STARFOX TRAVEL`, `RAIL RUN SECTOR 7 NO INPUT` y la línea de estado del host. No encuentra zonas cortadas, huecos negros, rectángulos de borrado ni píxeles ajenos; describe la escena como estática (es una sola captura).

### B — Dinámica/seguimiento (frames 0, 2, 4)

**Prompt:**

```text
Vas a analizar tres capturas consecutivas de la misma animacion (izquierda a derecha en el tiempo). Centrate en la nave cian del jugador y en los dos triangulos rojos. Sigue su evolucion frame a frame: posicion (zona relativa), tamano y aspecto; di si su trayectoria es suave y continua o si da saltos, se para o desaparece. Es coherente con 'la nave del jugador avanza por un rail y los enemigos se acercan y se van a los lados'? Responde en espanol y se concreto.
```

**Respuesta cruda (resumen literal):** la nave cian se mantiene en la misma posición (estacionaria); los dos triángulos rojos se mueven de forma suave y continua hacia la izquierda/derecha, acercándose al centro. El modelo considera que la nave no avanza por un rail y que los enemigos se acercan al centro, no a los lados.

**Contraste objetivo:** el modelo acierta el movimiento de los enemigos y falla al interpretar el rail. La nave del jugador está **fija respecto a la cámara** (con una oscilación pequeña) por diseño; el avance se representa acercando enemigos/estrellas, no trasladando la nave en pantalla. El gate `verify-3d-sequence` da **7/7 cambios** y 0 frames estáticos, y los frames muestran a los enemigos en posiciones y tamaños distintos. No es un defecto de contenido.

### C — Verificación dirigida con la intención (frame 2)

**Prompt:**

```text
Esta animacion PRETENDE mostrar un traveling no jugable estilo Star Fox: 1 nave cian del jugador en la parte baja central, 2 cazas rojos que se acercan y se van a los lados, disparos amarillos del jugador y rojos de los enemigos, estrellas en movimiento, un fondo espacial con banda superior, luna y rocas grises de primer plano. Para CADA elemento esperado indica en esta imagen: presente / ausente / a medias, y si su posicion y forma son coherentes. Despues responde: el movimiento seria continuo o hay saltos/costuras? algo parpadea o se corrompe? Senala la zona relativa (arriba/centro/abajo, izquierda/centro/derecha). No des por bueno nada; si algo es ambiguo, dilo.
```

**Respuesta cruda (resumen literal):** nave cian presente y coherente; cazas rojos presentes, aunque cuenta tres en vez de dos (sobreconteo); disparos amarillos presentes (dos puntos); disparos rojos no visibles en ese frame; estrellas, banda superior, luna y rocas presentes y coherentes. No ve parpadeo ni corrupción. De nuevo describe la imagen como estática (una sola captura).

**Contraste objetivo:** el recorte ampliado del centro muestra dos cazas rojos, la nave cian con cabina/fuego y los rastros amarillos de los disparos. Los disparos rojos son intermitentes (cada 53 frames) y no siempre están en el frame; su ausencia en una captura no es un fallo. El sobreconteo de cazas es una limitación del modelo en esta imagen.

### Evaluación canónica final

`src/VISION.md` contiene el prompt canónico de `tools/analyze/vision-prompt.txt` y la respuesta cruda de 6 frames. El modelo describe la nave azul, triángulos rojos que cambian de posición (a veces cuenta tres), estrellas, rocas/luna, bandas y textos; indica contornos limpios, fondo uniforme y ausencia de bandas, halos, píxeles cortados o restos de posiciones anteriores. La comprobación objetiva confirma 2 enemigos y el movimiento; el modelo no ve las balas pequeñas ni el desplazamiento de 1 px de las estrellas. Conclusión: no hay artefactos visibles ni elementos incoherentes.

## Conclusión y deuda

- **Correcto en el alcance probado:** render de la escena de 3 sólidos, animación de enemigos/estrellas/balas, fondos estáticos, HUD, 28,4 fps release, contrato de movimiento y flicker.
- **SIN VERIFICAR:** A500/CRT física, entrada (no hay), colisión (no hay), los modos opcionales `kAsteroids`/`kGates` (desactivados por presupuesto), y la continuidad más allá de la ventana de 8 frames capturada.
- **Deuda:** el router general de rutas de relleno por objeto (P8) sigue pendiente; esta demo usa una ruta local de spans CPU optimizada. El `plane_or_span` del engine se optimizó con una ruta por palabras solo en big-endian y se conserva la ruta por bytes en host; los tests host cubren la equivalencia. El 097 queda reservado para la demo de horizonte del plan de presentación.

## Anexo: respuestas crudas de depuración

### B (frames 0,2,4) — respuesta completa

La evolución de la animación entre las tres capturas muestra que la nave cian del jugador **no avanza por un rail**; su posición se mantiene casi idéntica en todos los frames, lo que indica que está **inmóvil** en el espacio. Por otro lado, los dos triangulos rojos, que son los enemigos, se mueven de forma **suave y continua** hacia la izquierda y derecha respectivamente, acercándose al centro del campo de visión.

**Análisis detallado:**

1.  **Nave del jugador (cian):**
    *   **Posición:** Se mantiene en el centro de la pantalla, en la misma zona relativa en los tres frames. No hay cambio en su posición horizontal ni vertical.
    *   **Tamaño y aspecto:** Su tamaño y forma son constantes en todos los frames. No hay variaciones en su aspecto.
    *   **Trayectoria:** **Estática**. No avanza, no se para, no da saltos ni desaparece. La etiqueta "NO INPUT" en la interfaz confirma que el jugador no ha dado ningún comando.

2.  **Enemigos (triangulos rojos):**
    *   **Posición:** El triangulo rojo izquierdo se desplaza ligeramente hacia la izquierda, y el triangulo rojo derecho se desplaza ligeramente hacia la derecha. Ambos se mueven hacia el centro, acercándose a la nave del jugador.
    *   **Tamaño y aspecto:** Su tamaño y forma permanecen constantes. No hay variaciones perceptibles.
    *   **Trayectoria:** **Suave y continua**, con un movimiento que sugiere una aproximación deliberada.

**Coherencia con la descripción:** la descripción "la nave del jugador avanza por un rail y los enemigos se acercan y se van a los lados" **no es coherente** con lo que se observa. La nave del jugador no avanza por un rail; está estacionaria. Los enemigos, en cambio, se acercan y se mueven hacia el centro, no hacia los lados. La animación parece ser una escena de prueba o un frame "freezed" donde la nave del jugador está detenida, y los enemigos están en movimiento hacia ella.

**Contraste:** el gate objetivo de secuencia da 7/7 cambios y la nave está fija respecto a la cámara por diseño (rail representado por el acercamiento de enemigos/estrellas). El modelo acierta el movimiento de los enemigos y falla al interpretar la metáfora del rail.

### C (frame 2) — respuesta completa

Análisis detallado de la imagen proporcionada:

**Elementos esperados vs. su presencia en la imagen:**

1.  **Nave cian del jugador (en la parte baja central):**
    *   **Presente:** Sí, hay una nave cian en la parte central inferior, justo encima de la franja de rocas y el texto. Es el elemento más prominente en la parte inferior.
    *   **Posición y forma coherentes:** Sí, está centrada horizontalmente en la parte inferior, como se esperaría. Su forma es un triángulo invertido (como un triángulo con la punta hacia arriba), lo cual es coherente con el estilo de *Star Fox* (aunque no es el clásico diseño de Arwing, es un triángulo que podría representar un vehículo). Su posición relativa es correcta para una nave del jugador en un escenario de "Rail Run".

2.  **2 cazas rojos que se acercan y se van a los lados:**
    *   **Presente:** Sí, hay tres formas triangulares rojas en la parte superior central. Sin embargo, *no hay exactamente dos*. Hay tres. Pero como se menciona "2 cazas rojos", y estos tres están agrupados y se parecen a ellos, se consideran coherentes con la descripción general de "cazas rojos".
    *   **Posición y forma coherentes:** Sí, están en la parte superior central, con una distribución que sugiere movimiento hacia los lados (uno a la izquierda, otro al centro-derecha, y uno más pequeño a la derecha). Su forma triangular es coherente con el estilo de los cazas enemigos de *Star Fox*. La posición de los tres en la parte superior central es coherente con "se acercan y se van a los lados" en un contexto de Rail Run.

3.  **Disparos amarillos del jugador y rojos de los enemigos:**
    *   **Presente:** Sí, hay dos pequeños puntos amarillos que parecen ser disparos. Uno está directamente sobre la nave cian, y otro está justo a la derecha de ella.
    *   **Posición y forma coherentes:** Sí, están en la parte central inferior, justo encima de la nave, lo que sugiere que son disparos del jugador. Sin embargo, no hay disparos rojos visibles en la imagen.

4.  **Estrellas en movimiento:**
    *   **Presente:** Sí, hay varios puntos pequeños de color azul claro dispersos por el fondo negro. Estos son las "estrellas".
    *   **Posición y forma coherentes:** Sí, están distribuidas por todo el fondo espacial, lo que es coherente con un entorno espacial. Sin embargo, no hay ninguna indicación de movimiento, ya que es una imagen estática.

5.  **Fondo espacial con banda superior:**
    *   **Presente:** Sí, el fondo es negro con estrellas, lo que representa el espacio. Hay una banda superior de color azul oscuro, que podría ser parte del espacio o una representación de la atmósfera, y una banda inferior de color azul oscuro también.
    *   **Posición y forma coherentes:** Sí, la banda superior es coherente con una representación de espacio, y la banda inferior es coherente con el suelo o la superficie planetaria.

6.  **Luna (o cuerpo celeste):**
    *   **Presente:** Sí, hay un cuerpo celeste azul claro en la parte inferior derecha. Aunque no se puede confirmar si es una luna, parece ser un cuerpo celeste.
    *   **Posición y forma coherentes:** Sí, está en la parte inferior derecha, lo que es coherente con una luna o planeta en un escenario espacial.

7.  **Rocas grises de primer plano:**
    *   **Presente:** Sí, hay varios cuerpos grises en la parte inferior, justo debajo de la nave cian. Son formas redondeadas o triangulares que parecen ser rocas o estructuras.
    *   **Posición y forma coherentes:** Sí, están en la parte inferior, lo que las hace parecer de primer plano. Su forma es coherente con rocas en un escenario de Rail Run.

**Respuesta a las preguntas:**

*   **El movimiento sería continuo o hay saltos/costuras?**
    *   **Respuesta:** Hay saltos/costuras. La imagen es estática, es una captura de pantalla. No hay movimiento continuo, como se podría esperar en un videojuego en acción. Los elementos están fijos en su posición, lo que sugiere que la animación se compone de varias imágenes estáticas (frames) con saltos entre ellas.

*   **Algo parpadea o se corrompe?**
    *   **Respuesta:** No hay evidencia de parpadeo ni corrupción. Todos los elementos están bien definidos y no parecen estar sufriendo cambios en su apariencia o posición entre frames.

**Conclusión:**

La imagen representa bien la animación descrita, pero con algunas variaciones (tres cazas rojos en lugar de dos). La disposición de los elementos es coherente con un escenario de *Star Fox*. La imagen es estática y no muestra movimiento continuo, por lo tanto, hay saltos/costuras. No hay parpadeo ni corrupción visible.
