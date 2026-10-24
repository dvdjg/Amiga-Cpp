# INTENT - 500 Caverna

Esqueleto V0/V1 de la vertical slice 2D: mapa fijo 256x20 celdas de 16 px, camara horizontal, jugador rectangular controlado por pad y HUD de host. Demuestra que un juego puede declarar el mapa fijo y registrar la capa de scroll con `App::add_scroll_layer` sin tocar `FramePlan` ni el backend.

No es todavia el juego completo: faltan DPF 3+3, colision, enemigos, audio y la prueba automatica de latencia.
