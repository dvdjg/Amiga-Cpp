# Intención de la demo 099 — Traveling estilo Star Fox

## Qué hace

Muestra un traveling **no jugable** inspirado en los niveles de espacio de Star Fox (SNES): el jugador lleva un caza en la parte baja central, una pareja de cazas enemigos entra por el fondo, se acerca por un rail fijo y se abre hacia los lados mientras intercambian disparos. El fondo es un campo de estrellas en movimiento, con una banda espacial superior, un planeta asomando, una luna y rocas grises de primer plano. La franja inferior muestra el nombre, el sector y el aviso `NO INPUT`; el overlay del host añade versión, frames y fps.

La cámara vive en el origen y mira hacia `+z`. El avance del rail se representa acercando los objetos en `z`; no hay entrada de usuario ni colisiones en esta fase.

## Qué pretende demostrar

Que se puede montar una escena 3D de juego con **modelos expresados en código** y una ruta de relleno adecuada al A500:

- Los modelos son sólidos convexos de 4-5 vértices construidos con `retro::MeshData` y registrados en `scene3d::ModelTable`.
- El render es **silueta plana**: se transforman y proyectan los vértices con `math3d`, se calcula la envolvente convexa 2D y se rellena con spans de CPU directos (`plane_or_span`) sobre dos planos.
- Se demuestra que, para objetos pequeños y pocos, esta ruta CPU es mucho más barata que el camino del engine por máscara 1bpp + cookie-cut por plano (que en esta escena costaba del orden de 70-90k ciclos por objeto). La escena de tres sólidos y los efectos caben en **~28 fps release**, por encima del suelo de 25 fps de las demos de relleno poligonal.
- El painter ordena los sólidos por `z`; los planos 2/3 quedan reservados al fondo estático y al HUD, de modo que el clear por frame solo toca los dos planos dinámicos.
- El HUD de host se refresca cada 8 frames para no pagar su coste (~25k ciclos) en cada frame.

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Caza del jugador | cian (COLOR01) | sólido de 5 vértices en la parte baja central; oscila y se inclina |
| Cabina y fuego del motor | amarillo (COLOR03) | rombo 2D de 5 filas y segmento de 6 px sobre el caza; no son sólidos independientes |
| Cazas enemigos (2) | rojo (COLOR02) | sólidos de 5 vértices que entran por el fondo, se acercan y se abren |
| Disparos del jugador | amarillo (COLOR03) | rastros verticales de 6 px que salen hacia el fondo cada 11 frames |
| Disparos enemigos | rojo (COLOR02) | rastros verticales ocasionales que vienen hacia la cámara |
| Estrellas | cian/amarillo | 24 puntos 3D que caen en `z` y se reciclan |
| Fondo superior | COLOR04/05 | banda espacial con un planeta azul asomando |
| Fondo inferior | COLOR05/04/09 | banda de suelo, línea de horizonte y luna azul |
| Rocas de primer plano | COLOR12/13 | tres octógonos grises estáticos en la banda inferior |
| HUD de host | blanco | nombre + número + versión + frames + fps + telemetría `E2 B?` |
| Rótulo de franja | COLOR15/14 | `099 STARFOX TRAVEL` y `RAIL RUN SECTOR 7 NO INPUT` |

Los planos 0/1 contienen el contenido dinámico (tres colores); los planos 2/3 contienen el fondo y el HUD estáticos. La zona de vuelo es `y = 16..175`; las bandas superior/inferior y la franja del HUD quedan fuera del clear.

## Evolución temporal

- El frame avanza una vez por actualización. El jugador oscila en `x`/`y` y aplica una inclinación suave (roll/pitch/yaw en Q4.12) con la tabla de seno.
- Los enemigos empiezan a `z ~390-460`, avanzan 5 unidades por frame y, por debajo de `z = 320`, se abren lateralmente hasta salir de pantalla; al alcanzar `z < 200` o el borde se reciclan al fondo con una nueva fase de weave.
- Los disparos del jugador salen cada 11 frames, alternando el ala izquierda/derecha, y se dibujan como un rastro fijo de 6 px; los enemigos disparan cada 53 frames desde el caza más cercano. El rastro no usa la cola 3D porque cerca del horizonte se proyectaba como un punto.
- Las estrellas caen con velocidad propia y se reciclan al pasar el near; el fondo estático (bandas, planeta, luna, rocas) no se toca durante la animación.
- El HUD de host se actualiza cada 8 frames; la telemetría muestra el número de enemigos y de balas activas.

## Presupuesto y alcance

- Build de referencia: `A500_release` (68000, OCS). Medido con `measure-fps`: **28,48 fps**, 249.041 ciclos/frame, 1,755 campos/frame, mínimo de ventana 27,3. El suelo de la demo es 25 fps por ser de relleno poligonal; no se declara excepción.
- El clear de la zona de vuelo (2 planos) cabe en el hueco de un campo; el coste que domina es el relleno de las tres siluetas y el fondo.
- Los asteroides y anillos **dinámicos** quedan como opción de compilación (`kAsteroids`/`kGates`) pero están desactivados en el perfil por defecto: el campo de asteroides visible es estático y las rocas de la banda inferior no cuestan nada por frame.
- Limitaciones: painter por objeto (sin z-buffer); los sólidos no se cruzan en la escena por defecto. No hay entrada, colisión, puntuación ni audio. La ruta de relleno es una optimización local de la demo; el router general de rutas del engine sigue pendiente en P8.
