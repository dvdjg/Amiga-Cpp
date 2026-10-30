# INTENT — 211 teleport de BOBs

## Qué hace

Tres BOBs de un plano rebotan sobre un framebuffer de 5 planos. Dos de ellos **saltan** cada 32 frames a una posición nueva, con un desplazamiento mayor que el padding; el tercero se mueve despacio. La demo usa `eng::scene::PlaneBobLayer` y fuerza su camino de degradación: cuando el salto supera el padding, la capa emite **clear de la huella previa + draw** en el plano del actor.

## Qué pretende demostrar

- Que la API no deja el BOB viejo al teletransportar: el frame del salto usa 5 jobs (`J5/B5`) y el siguiente vuelve a 3.
- Que el borrado se hace solo en el plano del actor y con `BLTDMOD=196`, sin tocar los otros cuatro planos.
- Que el camino de degradación sigue cabiendo en 50 fps con tres BOBs.

## Elementos en pantalla

- Fondo navy liso de 320×256.
- Tres BOBs circulares (rojo, azul, amarillo) en planos 0, 2 y 4.
- HUD de host con frames, fps y `J/B/D`; en el frame de teleport se ve el aumento de jobs.
- Canarios de memoria.

## Evolución temporal

1. **Init**: hoja, Copper list y primer frame; historial de la capa vacío.
2. **Frames normales**: movimiento de 1 px, un blit opaco por BOB (3 jobs).
3. **Teleport**: dos actores cambian de posición; la capa añade dos clear (5 jobs) y no queda copia vieja.
4. **Régimen**: 49,9 fps, secuencia de 40 frames sin banda inferior, cortes ni BOB duplicado.
