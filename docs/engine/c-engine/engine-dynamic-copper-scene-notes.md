# Copper dinamico y estado de escena

Guia de trabajo para evitar que la IA o el diseno del engine traten el copper como una lista estatica cuando en realidad depende del estado vivo de la escena.

## Idea central

El copper no es solo "efecto visual". En cuanto una escena lo usa de forma dinamica, pasa a formar parte del estado de render.

Eso implica distinguir:

- scaffolding fijo de display
- registros parcheados por frame segun la escena
- cambios que la copper realiza durante el barrido
- politica segura para actualizar la lista activa

## Regla principal

No describir una escena con copper dinamico solo como:

- "un gradiente"
- "unas barras"
- "un HUD con copper"

Hay que describirla como:

1. que variables de escena cambian;
2. que palabras de la copper list dependen de esas variables;
3. cuando las parchea la CPU;
4. cuando las ejecuta Agnus durante el barrido;
5. que garantiza que la lista no se corrompe a mitad de frame.

## Tipos de trabajo copper

### 1. Copper fija

Lista construida una vez:

- ventana de display
- bitplane config
- paleta estable
- waits y writes inmutables

Riesgo bajo.

### 2. Copper parcheada por frame

La estructura de la lista es fija, pero ciertas palabras cambian cada frame:

- colores
- scroll fino
- punteros
- modulos
- prioridades

Riesgo medio.

### 3. Copper dinamica de escena

La lista depende del estado de escena:

- numero de splits visibles
- posiciones de bandas
- cambios de viewport
- estados de HUD
- reacciones a gameplay o profiling

Riesgo alto.

### 4. Copper doble-buffered

La CPU rellena una lista mientras la otra esta activa.

Suele ser la opcion mas sana cuando la lista cambia mucho por frame.

## Preguntas obligatorias antes de implementar copper dinamico

1. Que parte de la lista es fija y que parte depende de la escena?
2. Que registros exactos cambian?
3. Cambian una vez por frame o varias veces dentro de la lista?
4. La CPU parchea una lista activa o una lista en back buffer?
5. Que pasa si el numero de cambios crece con la complejidad de la escena?
6. Esta tecnica realmente pertenece al copper o deberia resolverse con otra capa?

## Responsabilidades separadas

### CPU

- calcula el estado de escena
- decide los valores a emitir
- parchea lista o rellena back buffer
- instala la lista segura para el frame siguiente

### Copper

- ejecuta cambios en scanlines concretas
- modifica registros durante scanout
- aplica wait/write sin logica general de alto nivel

### DMA y display

- consumen la lista activa
- no deben ver una lista a medio parchear

## Invariantes utiles

- `COP1LC` apunta a la lista esperada
- los offsets parcheados por CPU corresponden a writes reales del copper
- la lista activa no se modifica fuera de la politica documentada
- `BPLCONx`, `COLORxx`, `BPLxPT`, `BPL1MOD/BPL2MOD` y registros similares solo cambian en puntos esperados
- la escena sigue coherente aunque cambie scroll, HUD o viewport

## Casos donde la IA suele fallar

### HUD con copper

La IA puede proponer "usar copper para fijar un HUD" sin responder:

- si el HUD depende de texto o valores variables
- si hay que cambiar la lista cada frame
- si conviene copper, plano overlay o sprites hardware

### Scroll fino con copper

La IA puede olvidar:

- que el scroll forma parte del estado por frame
- que el viewport y los modulos deben seguir coherentes
- que el copper puede necesitar cambios sincronizados con bitplane pointers

### Multiples bandas o splits

La IA puede no modelar:

- orden de waits
- rangos de lineas
- crecimiento de la lista segun la escena
- necesidad de doble buffer

## Politica recomendada para este repo

1. empezar por copper fija y casos de bateria pequenos;
2. promover luego copper parcheada por frame con invariantes claras;
3. usar doble buffer para cobre dinamico de escena cuando la lista deje de ser trivial;
4. no pedir a la IA efectos copper complejos sin contrato por frame y por scanline.

## Bateria sugerida

| Caso | Objetivo |
|------|----------|
| `C02_copper_double_basic` | alternancia segura entre dos listas |
| `C03_copper_palette_patch` | parcheo CPU por frame sobre una lista fija |
| `C04_copper_scroll_scene` | scroll o viewport con registros cambiantes |
| `C05_copper_hud_band` | banda fija dependiente de escena con politica documentada |

## Relacion con otros docs

- [amiga-lowlevel-agent-prompt.md](amiga-lowlevel-agent-prompt.md)
- [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md)
- [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md)
- [engine-roadmap.md](engine-roadmap.md)
