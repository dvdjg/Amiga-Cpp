# Intención de la demo 099 — Corneria rail travel

## Qué hace

Muestra un traveling **no jugable** inspirado en Corneria (primer nivel de Star Fox SNES): vuelo rasante sobre agua/terreno verde, cielo de día, un Arwing blanco/azul, dos cazas enemigos rojos, un arco de piedra y un edificio. La cámara viaja por una trayectoria zigzagueante con yaw/pitch/roll suaves. No hay entrada, colisión, puntuación ni audio.

## Qué pretende demostrar

Que se puede montar una escena con **todos los objetos en 3D** (naves, arco y edificio) y un fondo de cielo/terreno con **roll real** por variantes pre-rotadas, aun a costa de bajar de 6 fps:

- El fondo tiene tres variantes pre-rotadas (-6°, 0°, +6°) en Chip RAM; se elige la más cercana al roll de cámara y se copia con Blitter. La ventana de 320 px se desplaza en X (yaw) y en Y (pitch); el horizonte se inclina de verdad.
- Arwing: 23 vértices/31 triángulos con cabina elevada azul; enemigo: 11/14. Los quads se triangulan y las normales se orientan por el centro del modelo.
- El arco y el edificio son sólidos 3D con caras n-gon, culling y sombreado por el sol común. Se ordenan por `z` y se pintan con spans CPU opacos de 4 planos.
- La cámara añade yaw/pitch en la copia del fondo, roll real de fondo por variantes y roll/yaw/pitch sobre los objetos. El jugador combina dos senos en X y una oscilación suave en Y.
- El HUD de bitplanes queda fijo; el HUD de host se refresca cada 16 frames.

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Arwing del jugador | blanco/gris + alas azules, cabina azul | sólido 3D de 31 triángulos; oscila, rueda y cabecea |
| Cazas enemigos (2) | salmon/granate | sólidos 3D de 14 triángulos; weave y reciclaje |
| Arco | piedra clara/oscura | sólido 3D de 24 vértices; pasa por el centro |
| Edificio | gris | caja 3D de 12 vértices a la izquierda |
| Fondo | azul noche/medio, verde, azul agua | tres variantes pre-rotadas con horizonte inclinado |
| Disparos | blanco/salmon | rastros de 6 px |
| HUD de host | blanco | nombre + versión + frames + fps + `E2 B?` |
| Rótulo de franja | blanco/gris | `099 STARFOX TRAVEL` y `RAIL RUN CORNERIA NO INPUT` |

## Evolución temporal

- El jugador combina dos senos en X (zigzag), oscila en Y y aplica roll/pitch/yaw.
- Los enemigos avanzan en `z`, hacen weave y se reciclan.
- El arco y el edificio avanzan en `z`, se reciclan y pasan a los lados/centro.
- La cámara elige la variante de fondo por roll (tres pasos), desplaza la ventana por yaw/pitch y aplica yaw/pitch/roll a los objetos.
- Disparos del jugador cada 11 frames; enemigos cada 53; HUD de host cada 16.

## Presupuesto y alcance

- Build de referencia: `A500_release` (68000, OCS). Medido con `measure-fps`: **3,65 fps**, 1.943.341 ciclos/frame, 13,697 campos/frame, mínimo 3,1.
- **No cumple >6 fps**. Es la excepción aceptada por el usuario para que el arco y el edificio sean 3D reales; la versión con arquitectura estática medía 7,36 fps.
- Limitaciones: cielo/terreno/agua son fondo, no terreno poligonal 3D; painter sin z-buffer; dos objetos de arquitectura para no bajar aún más.
