# INTENT - 500 Caverna

Esqueleto V0/V1 de la vertical slice 2D: mapa fijo de 256×20 celdas de 16 px, cámara horizontal, jugador controlado por ratón o joystick, cuatro enemigos como sprites hardware y HUD de host. Demuestra que un juego puede declarar el mapa fijo y registrar la capa de scroll con `App::add_scroll_layer` sin tocar `FramePlan` ni el backend, y que la entrada llega por mensajes del mini-OS.

El fondo usa DPF 3+3 a 320×208 y scroll XLimited; el jugador y los enemigos son sprites hardware. No hay audio. Todavía faltan la colisión contra tiles, la música, el manifiesto de assets y la prueba automática de latencia.
