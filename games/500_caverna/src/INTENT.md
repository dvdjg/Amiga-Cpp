# INTENT - 500 Caverna

Vertical slice 2D (V0/V1/V2/V3) en `games/500_caverna`: mapa fijo de 256×20 celdas de 16 px, cámara horizontal, jugador con salto/gravedad/colisión, ocho enemigos con tipos, patrulla y animación de arte, DPF 3+3 a 320×208 y HUD de host. El arte procedural vive en `caverna_art.cpp`; la lógica de juego no dibuja píxeles ni toca planos.

Demuestra que un juego declara el mapa y las capas con `App::add_scroll_layer`, registra su pasada de actores con `App::add_sprite_pass` (`SpriteDirectorPass`) y consume la entrada por mensajes del mini-OS. El `App` ejecuta la pasada de sprites antes del scroll; la escena materializa placements HW y BOB/CPU, y el `App` compone.

El movimiento visible se actualiza cada frame; la animación de arte va cada 8 frames. El paso del jugador es 1 px/frame porque con 2 px/frame el frame roza el límite de campo (medido 40,8 fps). No hay audio, por decisión explícita. Siguen pendientes las plataformas de tilemap más ricas, la reacción completa de enemigos, el manifiesto de assets y la prueba automática de latencia; la sonda de salto sigue sin confirmar (`SALTO: NO`).
