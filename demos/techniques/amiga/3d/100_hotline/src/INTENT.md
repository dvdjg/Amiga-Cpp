# Intención de la demo 100 - HOTLINE (Superhot-like 2.5D)

## Qué hace

Prototipo de juego first-person por nodos inspirado en Superhot: el tiempo solo avanza cuando el jugador se mueve. La sala es 2.5D con fondo pre-renderizado, tres personajes con malla esqueletada de 55 vértices y seis huesos, coberturas, arma y HUD poligonal.

## Qué pretende demostrar

- Render 3D por caras exactas con 3 planos y 5 colores, usando una máscara 1bpp por color y cookie-cut por plano en lugar de un blit por cara.
- Skinning rígido con transición de pesos en hombros, caderas y cuello sobre una malla única.
- Tiempo detenido: solo el movimiento del jugador avanza el mundo; girar y disparar no consumen tiempo.
- Un vertical slice completo con disparo, colisión, muerte, victoria y reinicio sin portada ni menú.

## Elementos en pantalla

- Sala 2.5D con cuatro vistas pre-renderizadas, suelo, paredes, coberturas y fondo.
- Tres enemigos low-poly con cabeza, torso, brazos y piernas; animación de poses y apuntado.
- Jugador en primera persona, arma, mirilla, chevron del logo, pips de enemigos y estado.
- OSD de depuración del host con nombre, versión, frame y fps.

## Evolución temporal

- La demo se auto-demuestra a los 6 s sin input.
- En reposo el mundo está detenido; al avanzar, los enemigos y las balas se mueven por ticks y las poses cambian.
- El disparo elimina pips y puede terminar en victoria; el reinicio vuelve al estado inicial.
- La cámara avanza/retrocede por nodos y gira 90 grados; las vistas de la sala cambian con el nodo.
