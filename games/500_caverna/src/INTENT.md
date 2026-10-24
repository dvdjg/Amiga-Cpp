# INTENT - 500 Caverna

Esqueleto V0/V1/V2/V3 de la vertical slice 2D: mapa fijo de 256×20 celdas de 16 px, cámara horizontal, jugador con salto y gravedad, cuatro enemigos patrullando como sprites hardware y HUD de host. Demuestra que un juego puede declarar el mapa fijo y registrar la capa de scroll con `App::add_scroll_layer` sin tocar `FramePlan` ni el backend, y que la entrada y la lógica de frame llegan por mensajes del mini-OS.

El fondo usa DPF 3+3 a 320×208 y scroll XLimited; el jugador colisiona con el tilemap y los enemigos se mueven y animan dentro de los cinco canales de sprite. No hay audio, por decisión explícita para esta fase. Todavía faltan plataformas de tilemap más ricas, más actores, manifiesto de assets y la prueba automática de latencia.
