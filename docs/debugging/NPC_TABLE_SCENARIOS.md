# Bitácora: laboratorio de escenarios de mesa (`eng::sim` + `eng::cards`)

Registro de las **situaciones de mesa** con las que se pone a prueba la capa de psicología de
NPC (`docs/engine/architecture/NPC_PSYCHOLOGY.md`). No es documentación de referencia del
engine ni del ecosistema de criaturas (esa vive en `SIM_ECOSYSTEM_SCENARIOS.md`): aquí se
describen escenarios concretos, qué rasgo o mecanismo ejercitan y cómo se observan.

Un escenario es una **trayectoria** de eventos sobre una mesa, no un test unitario. La misma
maquinaria (persona, expresión, lectura, psique, convención) se pone en marcha y se comprueba
que evoluciona como debe; los tests host por pieza están en `tests/host/199`…`206` y el
detector de regresión de nivel en `tools/cards/regression.sh`.

## Cómo se observan

```bash
# Partidas CPU vs CPU con la misma configuración que el juego (estilos + rango + perfil).
tools/cards/selfplay.sh 500 --seats 6 --seed 7 --profile N512

# Detector de regresión de nivel por footprint (no debe degradarse).
tools/cards/regression.sh
```

En el juego `games/200_holdem` cada asiento tiene una persona y un avatar que refleja sus
tells; en `games/100_chess` y `games/101_go` el NPC usa **introspección simulada**
(`sim/introspection.hpp`) para expresar confianza, duda o presión desde su propia búsqueda y
para impacientarse si el humano tarda (`gestures_for_pace`).

## Escenarios

| # | Escenario | Persona / disparador | Qué se espera observar |
|---|---|---|---|
| 1 | **El pardillo** | arquetipo ingenuo, `deceit` bajo, poca pericia | fuga alta y legible (tells claros); pierde en el showdown cuando se le lee |
| 2 | **El listillo** | `deceit` alto, `suspicion` alta, pericia media | tells invertidos (finge) y descuento de la lectura ajena; no siempre gana, pero no se le explota |
| 3 | **El observador** | `read` neutral, prior plano | tras N showdowns, su `ReadModel` separa el tell del rival del ruido (`p_strong` sube) |
| 4 | **El irascible en tilt** | `anger`/`irritability` altos | un *bad beat* dispara `tilt`; sube la agresión y la fuga, y el efecto se disipa con el tiempo |
| 5 | **El flemático** | compostura y estabilidad altas | ante la misma racha que el irascible, apenas se le nota; mantiene el plan |
| 6 | **El cazador de convenciones** | `suspicion` alta, observa a dos aliados | `infer_convention` detecta la correlación gesto→señal y explota el pacto |
| 7 | **El farol leído** | emite `raise` con mano débil | el tell autonómico se escapa (microexpresión) y un observador atento ajusta su rango |
| 8 | **El humano lento** | sin entrada durante muchos frames | el NPC bosteza/resopla (`gestures_for_pace`) y, con esperas largas, se desploma |
| 9 | **El humano que se derrumba** | pierde varias manos seguidas | el NPC sube su `confidence` (introspección del rival) y, si procede, presiona más |
| 10 | **El motor confiado** | búsqueda con margen claro o libro | el NPC de ajedrez/Go sonríe y juega seguro; con margen fino, entrecierra los ojos (duda) |

Cada escenario combina piezas ya verificadas: la trayectoria no necesita un test nuevo, pero
**sí** que su combinación no se degrade; de ahí el detector de regresión en `tools/cards/`.

```text
   evento de mesa            persona            estado              salida visible
   ┌──────────────┐     ┌──────────────┐   ┌──────────────┐    ┌──────────────────┐
   │ bad beat     │ ──► │ irascible    │──►│ tilt↑ fuga↑  │──► │ gestos + lectura │
   │ showdown     │ ──► │ observador   │──►│ ReadModel    │──► │ rango ajustado   │
   │ espera larga │ ──► │ cualquiera   │──►│ boredom      │──► │ bostezo/desplome │
   └──────────────┘     └──────────────┘   └──────────────┘    └──────────────────┘
```

## Hallazgos y ajustes

- **La lectura necesita showdown**: sin etiqueta (mano revelada) el observador no aprende; el
  pardillo sigue siendo explotable hasta que se le ve suficiente. Ajuste: el aprendizaje solo
  en `label_showdown`.
- **El listillo no es inmune al azar**: fingir con `deceit` bajo produce tells **invertidos**
  que se leen igual (peor que no fingir). Ajuste: `deceit` modula la credibilidad del tell
  falso, no la elimina.
- **El tilt sin deriva se queda pegado**: un `tilt` que no decae convierte al irascible en un
  maníaco permanente. Ajuste: `psyche_update` deriva el estado por pasos (`PsycheParams`).
- **La espera también es un tell**: un motor mudo parece roto; con `gestures_for_pace` la
  paciencia del NPC es información, no silencio.

## Cuestiones abiertas

- Las **señales voluntarias del humano** (burlarse, amenazar, calmar) ya se emiten
  (`expression_from_input`), pero el NPC todavía no modula su estado con ellas; es el paso
  P6.4.
- Un escenario **multimesa** (varios NPC con memoria de sesiones anteriores) exige persistencia
  entre partidas, fuera del alcance actual.
- Medir objetivamente si un escenario "se siente vivo" requeriría validación visual (Ollama),
  pendiente mientras no haya ejecución fiable de WinUAE en este entorno.
