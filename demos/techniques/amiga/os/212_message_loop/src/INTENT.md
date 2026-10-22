# INTENT — 212_message_loop

## Qué hace

La demo 212 es el banco de pruebas del **bucle de mensajes** del mini-SO (`eng::os`) sobre el motor: el VBlank no se sondea, se convierte en el latido que publica el frame, la entrada y los timers. El `Engine` corre en modo cooperativo: el SO conserva el display y el autovector de nivel 3, el engine encadena su servidor con `AddIntServer`, levanta una señal Exec y la tarea principal duerme en `Wait()`. El tick del mini-SO (`os::tick`) corre dentro de la IRQ de VBlank; `update`/`render` corren en el bucle principal.

En pantalla, una caja naranja se mueve con el joystick del puerto 2 (o el ratón del puerto 1) y el panel muestra los contadores del sistema: `frames` (latidos de VBlank), `msgs` (mensajes drenados), `joy`, `keys`, `bg` (slices de idle de la tarea de fondo), `t` (timers), `pad` (CD32) y `last` (último rawkey). La demo no dibuja un efecto de chipset: la evidencia es el estado del mini-SO y el movimiento de la caja.

## Qué pretende demostrar

Demuestra que un juego/demo puede ser **dirigido por mensajes** sin sondear `VPOSR`, sin escribir `INTENA` y sin tomar el Copper: el VBlank llega como señal/notificación, la entrada llega como mensajes (`Joystick`, `MouseMove`, `KeyDown`, `Gamepad`) y los timers como `Timer`. También demuestra los tres productos del mini-SO: el puerto, la tarea de fondo que solo recibe idle en frames sin mensajes y el timer de periodo 2 (`os::add_timer(1u, 2u)`). El HUD es el overlay del host, compuesto con `eng::util::TextLine`/`operator<<` (sin buffer crudo ni `append` manual), y la franja de bitplanes queda libre.

El objetivo técnico incluye el **modo cooperativo** validado en KS 1.3: el SO mantiene su display, el engine usa `AddIntServer` y `Wait`, y el tick de entrada/timers no requiere polling. La demo sirve de contraste con la 224 (display propio, `Exclusive` + sondeo) y con el modo `Active`.

## Elementos en pantalla

- Panel rectangular azul oscuro con borde blanco, título `AMG demo 212 - mini-OS message loop` y subtítulo `VBlank + input por mensajes (sin sondear hardware)`.
- Línea de estado en amarillo: `frames`, `msgs`, `joy`, `keys`, `bg`, `t`, `pad` y `last`.
- Caja naranja de 20×20 px que se desplaza horizontal y verticalmente dentro del panel.
- Instrucciones en gris: mover la caja con el joystick del puerto 2 o el ratón; `bg` son los slices de idle de la tarea de fondo (M10).
- Todo el texto y el panel son overlay de depuración del host (`app.debug()`), no bitplanes del Amiga.

## Evolución temporal

Al arrancar, el panel aparece con los contadores a cero; el primer VBlank incrementa `frames` y el tick drena la cola. Cada VBlank vuelve a publicar el latido; `t` avanza cada dos frames por el timer de periodo 2; `msgs` sube cuando hay entrada o timer; `bg` avanza solo en los frames sin mensajes (el frame/entrada siempre gana). Al inyectar `--joy 1:right` la caja se desplaza y `joy` refleja la dirección mientras el evento está activo; al inyectar `--key-events 140` aparece `keys` y `last: 0x003d`. La secuencia de capturas muestra los contadores creciendo y la caja en su nueva posición; el HUD no se solapa porque se limpia antes de cada actualización.

La sincronización es por el latido del VBlank (señal Exec), no por sondeo de `VPOSR`; la demo duerme en `Wait()` cuando no hay trabajo de fondo. El modo cooperativo no hace `Forbid`, no llama a `LoadView`, no toca `COP1LC`/`DMACON` y no escribe `INTENA`: el display y la interrupción pertenecen al SO.
