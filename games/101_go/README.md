# `games/101_go` — Go 9×9 sobre el engine

Consumidor real del motor de Go (`eng::board::go`). El jugador lleva las **negras**
con el joystick y el motor juega las **blancas**.

## Controles

- **Joystick**: mueve el cursor por las 9×9 intersecciones.
- **Fuego**: coloca una piedra negra si la jugada es legal (prohibido el suicidio y
  el ko simple); el motor responde de inmediato.

## Qué motor usa

- `eng::board::GoRules`: grupos y libertades (flood fill), captura de grupos, suicidio
  y ko simple; la jugada lleva flag de captura.
- `GoSearcher` = `Searcher<GoRules, GoEval, GoOrdering>`: el **mismo buscador
  genérico** que el ajedrez (negamax + alpha-beta + iterative deepening + quiescence +
  TT) a profundidad 2.
- `eng::board::go::evaluate_black`: territorio (regiones vacías rodeadas por un color),
  capturas y penalización de grupos en atari.

## Visual

escena EHB `scene::compose` (320×256, 6 planos). Se dibuja la rejilla, las piedras (círculos
por CPU), el cursor y el estado. El tablero solo se redibuja cuando hay jugada o se
mueve el cursor.

En la cabecera se dibuja una **cara del NPC** (las blancas) que refleja su estado desde la
**introspección simulada** (`eng/sim/introspection.hpp`): sonríe si su búsqueda está clara,
entrecierra los ojos si duda y **bosteza/se impacienta si el humano tarda**
(`gestures_for_pace`). Ver `docs/debugging/NPC_TABLE_SCENARIOS.md` y HOST-206.

## Estado

- **Compila y enlaza** (`build-demo.sh`, verificado) y con el toolchain m68k.
- **Ejecutado en WinUAE** (`run-demo.sh --warp`): READY + captura;
  `analyze-demo.sh` da **OK**. Evidencia build → run → analyze.
- **Pendiente**: pase/dos pases, superko, patrones de fuseki, conteo final de
  territorio con zonas muertas y pulido visual.

## Build / run / analyze

```bash
bash tools/build/build-demo.sh games/101_go --debug --clean
bash tools/run/run-demo.sh games/101_go
bash tools/analyze/analyze-demo.sh games/101_go
```
