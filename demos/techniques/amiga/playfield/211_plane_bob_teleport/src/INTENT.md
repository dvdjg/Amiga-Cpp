# INTENT — 211 BOBs de un plano a velocidad alta

## Qué hace

Tres BOBs de un plano rebotan sobre un framebuffer de 5 planos. Los tres avanzan 1 px/frame con padding 0; la demo usa `eng::scene::PlaneBobLayer` y fuerza su camino de degradación: cada frame la capa emite **clear de la huella previa + draw** en el plano del actor. Las trayectorias son continuas, con rebote en los bordes.

## Qué pretende demostrar

- Que la API no deja el BOB viejo al moverse: cada frame usa 6 jobs (`J6/B6`) y la trayectoria no tiene saltos.
- Que el borrado se hace solo en el plano del actor y con `BLTDMOD=196`, sin tocar los otros cuatro planos.
- Que el camino de degradación sigue cabiendo en 50 fps con tres BOBs y movimiento continuo.

## Elementos en pantalla

- Fondo navy liso de 320×256.
- Tres BOBs circulares (rojo, amarillo y gris) en planos 0, 2 y 3.
- HUD de host con frames, fps y `J/B/D`.
- Canarios de memoria.

## Evolución temporal

1. **Init**: hoja, Copper list y primer frame; historial de la capa vacío.
2. **Frames**: cada actor avanza 1 px, rebota en los bordes y la capa limpia su huella anterior en el mismo frame.
3. **Régimen**: 49,9 fps, secuencia de 30 frames sin bandas, cortes, restos ni saltos.
