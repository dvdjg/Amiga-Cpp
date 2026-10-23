# INTENT — 065_mechanisms_bridge

## Qué hace

La demo 065 es el tutorial de los **mecanismos de escenario** del engine: un puente colgante con física **verlet** (`scene::Rope`), un jugador que camina y se apoya en él, una catapulta que carga/dispara y lanza una bola con una trayectoria balística cocinada, un objetivo **destructible**, una **puerta deslizante** y un **bloque sólido**. La puerta se abre y cierra con el ciclo de la catapulta; el bloque y la puerta cerrada detienen al jugador mediante cajas AABB (`SolidBox`/`solid_overlap`), y el objetivo usa el `Destructible` del engine: el impacto de la bola lo rompe y suelta cuatro escombros con trayectorias de caída.

El render sigue la receta rápida de las demos de mecanismos: hojas con **padding transparente** y copia opaca (FastCopy) para los objetos que no se solapan, **cookie-cut** con máscara más caja de borrado para los que sí (jugador, brazo, bola, escombros, puerta), copperlist estática emitida una vez y ejecución **async** del `FramePlan`.

## Qué pretende demostrar

Demuestra que las piezas de juego de `eng/scene` se combinan sin lógica de hardware en la demo: `Rope` (puente deformable), `Catapult` (máquina de estados y extremo por tabla de seno), `Trajectory`/`TrajectoryFollower` (bola y escombros), `Door` (apertura/cierre temporizado, `door_solid`), `SolidBox`/`solid_overlap` (colisión AABB del jugador) y `Destructible` (vida y golpe final). La demo es el banco visual de esas APIs: la puerta se ve subir y bajar, el bloque detiene al jugador y el objetivo se rompe con escombros, todo con el contrato de render FastCopy/cookie-cut y la telemetría de la franja.

También conserva el presupuesto medido: la demo es una excepción documentada al objetivo de 50 fps; el coste está dominado por la cadena de blits planar de 2 planos y el siguiente paso anotado es el target interleaved, no añadir más capas.

## Elementos en pantalla

- **Puente**: 12 eslabones cuadrados amarillos entre dos anclajes, con catenaria que se deforma al pisarla o recibir impulsos.
- **Jugador**: caja blanca de 8×10 px que avanza de lado a lado, cae con gravedad y se apoya en la cuerda o en el suelo.
- **Catapulta**: base y brazo a la derecha; el brazo se carga, dispara y el extremo sigue una tabla de seno.
- **Bola y escombros**: bola pequeña que sigue la parábola cocinada; al impactar en el objetivo, cuatro escombros caen por trayectorias de caída.
- **Objetivo destructible**: caja cálida de 14×14 que se rompe con el impacto y reaparece tras la espera.
- **Puerta**: barra vertical cálida de 12×24 apoyada en el suelo; sube 24 px al abrirse y vuelve a bajar al cerrarse con el ciclo.
- **Bloque sólido**: caja blanca de 16×10 sobre el suelo a la derecha; el jugador se detiene en su borde.
- **Franja inferior**: nombre y versión de la demo, más la telemetría de fps/ciclos.

## Evolución temporal

Al arrancar, el puente se asienta y el jugador cae hasta apoyarse. La catapulta alterna carga y disparo cada 240 ticks; al cruzar el ángulo de suelta nace la bola, que vuela hacia el objetivo y lo rompe, dejando escombros que caen hasta el suelo. El objetivo reaparece 300 ticks después. En paralelo, la puerta se abre a los 20 ticks del ciclo y se cierra a los 150: mientras está cerrada, el jugador que llega a su caja invierte la marcha; cuando está abierta, pasa y sigue hasta el bloque sólido, donde vuelve a detenerse. La secuencia de capturas debe mostrar al jugador, el puente deformado, el brazo/bola, la puerta en dos posiciones y el bloque en el camino.
