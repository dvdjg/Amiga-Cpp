# Intención de la demo 099 — Traveling estilo Star Fox

## Qué hace

Muestra un traveling **no jugable** inspirado en los niveles de espacio de Star Fox (SNES): el jugador lleva un caza en la parte baja central, una pareja de cazas enemigos entra por el fondo, se acerca por un rail fijo, se abre hacia los lados y se inclina/cabecea mientras intercambian disparos. El fondo es un campo de estrellas en movimiento, con una banda espacial superior, un planeta asomando, una luna y rocas grises de primer plano. La franja inferior muestra el nombre, el sector y el aviso `NO INPUT`; el overlay del host añade versión, frames y fps.

La cámara vive en el origen y mira hacia `+z`. El avance del rail se representa acercando los objetos en `z`; no hay entrada de usuario ni colisiones en esta fase.

## Qué pretende demostrar

Que se puede montar una escena 3D de juego con **modelos expresados en código** y **sombreado plano por caras** en el A500, con una paleta corta y una única fuente de luz:

- Los modelos son sólidos convexos de 5 vértices y 6 caras triangulares construidos con `retro::MeshData` y registrados en `scene3d::ModelTable`.
- El render proyecta todos los vértices y pinta cada cara visible por scanlines con `plane_or_span` sobre **3 planos dinámicos** (8 colores). Las caras traseras se descartan por el signo de la normal rotada.
- Cada cara guarda una normal local normalizada, calculada una vez al construir el modelo. En runtime se rota con la nave y se compara con la dirección a un **sol común**; el ángulo elige una de las **tres intensidades** de la gama de la nave.
- El jugador usa una gama teal (clara/medio/oscura) y los enemigos una gama salmon/ladrillo/granate. El color 7 (luz cálida) se comparte para el sol, el motor, las balas y las estrellas. Los índices de color se ordenan para que la cara grande iluminada use un solo bit y las sombras usen dos.
- La geometría tiene grosor (`hy` 10/9/8) para que las caras superior/lateral se vean y la iluminación no quede plana; los enemigos además hacen un weave en X/Y y cambian su roll/pitch/yaw, de modo que la trayectoria se lee mejor.
- El painter ordena los sólidos por `z`; el plano 3 queda reservado al fondo estático y al HUD con índices 8..15. El clear por frame toca los tres planos dinámicos.
- El HUD de host se refresca cada 16 frames para no pagar su coste (~25k ciclos) en cada frame.
- **Presupuesto**: esta ruta de caras reales cuesta ~2x la silueta de dos tonos y queda en **19,57 fps**. No cumple el suelo de 25 fps; es una excepción documentada por el salto visual solicitado. La silueta de dos tonos a 26,42 fps está en el commit `fd04dc87` para poder volver a ella.

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Caza del jugador | teal claro/medio/oscuro (COLOR01/02/03) | sólido de 5 vértices y 6 caras en la parte baja central; oscila, se inclina y rueda |
| Cazas enemigos (2) | salmon/ladrillo/granate (COLOR04/05/06) | sólidos de 5 vértices y 6 caras que entran por el fondo, se acercan, hacen weave y ruedan |
| Fuego del motor | luz cálida (COLOR07) | segmento directo de 6 px bajo el caza |
| Disparos del jugador | luz cálida | rastros verticales de 6 px que salen hacia el fondo cada 11 frames |
| Disparos enemigos | salmon claro | rastros verticales ocasionales que vienen hacia la cámara |
| Estrellas | teal claro/luz cálida | 20 puntos 3D que caen en `z`, se reciclan y no se dibujan encima de una nave |
| Fondo superior | azul noche/marino | banda espacial con un planeta azul pizarra asomando |
| Fondo inferior | azul marino/noche | banda de suelo, luna azul y rocas grises |
| HUD de host | blanco roto | nombre + número + versión + frames + fps + telemetría `E2 B?` |
| Rótulo de franja | blanco roto/dorado apagado | `099 STARFOX TRAVEL` y `RAIL RUN SECTOR 7 NO INPUT` |

Los planos 0/1/2 contienen el contenido dinámico (8 colores); el plano 3 contiene el fondo y el HUD estáticos (índices 8..15). La zona de vuelo es `y = 16..175`; las bandas superior/inferior y la franja del HUD quedan fuera del clear.

## Evolución temporal

- El frame avanza una vez por actualización. El jugador oscila en `x`/`y` y aplica una inclinación suave (roll ±26°, pitch ±10°, yaw ±7° en Q4.12).
- Los enemigos empiezan a `z ~390-460`, avanzan 5 unidades por frame y, por debajo de `z = 320`, se abren lateralmente hasta salir de pantalla; además mezclan un weave X/Y (26/22 unidades) con un **bank** que crece con el drift y con pitch/yaw oscilantes (±260/±380 en Q4.12). Al alcanzar `z < 200` o el borde se reciclan al fondo con una nueva fase.
- Los disparos del jugador salen cada 11 frames, alternando el ala izquierda/derecha, y se dibujan como un rastro fijo de 6 px; los enemigos disparan cada 53 frames desde el caza más cercano.
- Las estrellas caen con velocidad propia y se reciclan al pasar el near; el fondo estático (bandas, planeta, luna, rocas) no se toca durante la animación.
- El HUD de host se actualiza cada 16 frames; la telemetría muestra el número de enemigos y de balas activas.

## Presupuesto y alcance

- Build de referencia: `A500_release` (68000, OCS). Medido con `measure-fps`: **19,57 fps**, 362.399 ciclos/frame, 2,554 campos/frame, mínimo de ventana 18,9. **No cumple el suelo de 25 fps**; excepción documentada: el usuario pidió priorizar caras reales con luz común y commitear. La variante de silueta de dos tonos a 26,42 fps queda en el historial.
- El coste dominante es el relleno de las caras (3 planos, una o dos escrituras por píxel según la intensidad) y el clear de los 3 planos dinámicos. El fondo es estático y el HUD de host se refresca cada 16 frames.
- Los asteroides y anillos **dinámicos** siguen como opción de compilación (`kAsteroids`/`kGates`) pero están desactivados en el perfil por defecto: el campo de asteroides visible es estático y las rocas de la banda inferior no cuestan nada por frame.
- Limitaciones: painter por objeto (sin z-buffer); los sólidos no se cruzan en la escena por defecto. No hay entrada, colisión, puntuación ni audio. El sombreado por caras en CPU no cabe en 25 fps; la siguiente ruta para conservar caras y presupuesto es pre-renderizarlas como BOBs/sprites.
