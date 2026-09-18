# Bitácora: laboratorio de escenarios del ecosistema (`eng::sim`)

Registro de la puesta a punto del ecosistema mediante simulaciones largas
(`tests/host/173_sim_scenarios`). No es documentación de referencia del engine (esa vive en
`docs/engine/architecture/SIM_ECOSYSTEM.md`): aquí se anotan los **experimentos, los
problemas encontrados y los ajustes** con los que el sistema pasó de extinguirse a comportarse
como un mundo vivo.

## Cómo se ejecuta

```bash
bash tools/run-host-tests.sh tests/host/173_sim_scenarios
```

Cuatro escenarios (`abundante`, `escaso`, `depredadores`, `manada`), 4000 ticks cada uno, con
un digesto cada 500: población (por especie), nacimientos, muertes por causa (hambre `H`,
vejez `A`, depredación `P`), medias de necesidades y actividad cultural. El arnés implementa
reglas de juego mínimas (percepción, caza, comer, cortejo, reproducción y, al morir,
clasificación de la causa) para ejercitar el engine completo.

## Iteraciones y hallazgos

| # | Síntoma observado | Causa raíz | Ajuste |
|---|---|---|---|
| 1 | Extinción total antes del primer digesto | el arnés solo alimentaba si `room_food > 80` y ningún bioma de las primeras 6 regiones llegaba a 80 | comer del bioma con umbral de abundancia por escenario (`food_bias`) |
| 2 | Nacimientos, pero la **segunda generación** moría sin descendencia | `repro_min_health` (50) era mayor que `newborn_health` (40): las crías nunca podían reproducirse | `repro_min_health = 30`; las crías maduran y crían |
| 3 | La población se llenaba una vez y luego decaía sin recuperarse | `spawn` no reutilizaba huecos de muertos: al llenarse, no había sitio para reemplazos | `spawn` reutiliza huecos de criaturas muertas (reciclado de capacidad) |
| 4 | Crecimiento sin freno de los depredadores; presas al borde de la extinción | el arnés también alimentaba a los depredadores del bioma | los depredadores dependen solo de la caza → aparece presión/regulación |
| 5 | Los depredadores morían de hambre con presas cerca | su percepción (alcance ~8-13) no alcanzaba a las presas en la sala | sentidos de laboratorio (alcance 30+) para que haya encuentros |
| 6 | Cazas "fantasma": depredadores persiguiendo criaturas equivocadas | el reciclado reutilizaba ids y las referencias antiguas (trackers) aliaseaban la nueva criatura | `SimWorld` usa ids **monótonos** con huecos reciclados: ninguna referencia antigua apunta por accidente a un reciclado |

Los puntos 2, 3 y 6 son correcciones de **engine** (afectan a cualquier juego que use
`SimWorld`), no solo al laboratorio. El 6 es el más sutil: reutilizar ids sin generación
produce comportamientos incoherentes difíciles de depurar.

## Comportamiento actual (tras los ajustes)

- `abundante`/`escaso`: población sostenida y estabilidad; nacimientos y muertes por vejez
  continuos (cohortes que se renuevan), con la comida como factor limitante (`escaso` mantiene
  el hambre media más alta sin llegar a la inanición masiva).
- `depredadores`: ciclo depredador-presa. Con 8 depredadores y 20 presas, los depredadores
  primero cazan (varias muertes), luego dependen del encuentro; si las presas escasean, los
  depredadores declinan y las presas se recuperan. Es la **inestabilidad típica** de un
  sistema Lotka-Volterra pequeño y cerrado.
- `manada`: la coordinación de manadas agrupa a los miembros alrededor de la presa; la
  actividad social/ritual crece con la población.

## Analogías con el mundo real

- **Capacidad de carga**: la población satura la capacidad del escenario; bajar el alimento
  reduce el techo (como en un ecosistema limitado por recursos).
- **Ciclos depredador-presa**: la presión de caza reduce presas, lo que reduce depredadores.
- **Sucesión de cohortes**: la vejez programada (edad) produce relevo generacional, no una
  población estática.
- **Comportamiento social emergente**: la reproducción solo sostiene el mundo si hay
  **cortejo y pareja** (punto 2/3): sin formación de vínculos, cada generación muere sin
  relevo. Es la analogía algorítmica de "sin vínculos no hay continuidad".
- **Cultura heredada**: las tradiciones se pierden si nadie queda para enseñarlas (el
  contador de rituales cae cuando el grupo social desaparece).

## Cuestiones abiertas

- Los ciclos depredador-presa son **frágiles** a escala pequeña; para estabilidad haría falta
  refugio de presas, dieta parcial de los depredadores o reproducción más rápida de la presa.
- **Resuelto**: el tope global de población se sustituyó por **aforo por región** (capacidad
  del bioma, `biome.hpp`), y la carga se reparte por **LOD alrededor del jugador**
  (`lod.hpp`: realized/abstract/dormant; HOST-174).
- La caza es por contacto; sin "emboscada" ni persecución larga, los depredadores dependen del
  azar del encuentro. La coordinación de manada mejora la captura (HOST-172) y es la vía para
  tácticas más ricas.

## Iteración 2: aforo por región, jugador y LOD

Tras la primera puesta a punto se abordó el **punto de vista del jugador** y el **coste**:

- **Aforo por región** (`biome.hpp` + `SimWorld::region_capacity/population/has_space`): cada
  bioma sostiene un número finito de criaturas; `try_reproduce`/`lay_brood` no inician cría si
  la región está llena. Sustituye al tope global por uno **ecológico** (desierto 5, ciénaga 8,
  bosque 14...).
- **LOD** (`lod.hpp`): `update_lod(px, py, room)` marca **realized** en un radio, **abstract**
  a media distancia y **dormant** fuera; los dormidos **no se simulan** (no cuestan CPU) y se
  despiertan al acercarse el jugador. Así el jugador ve un mundo rico cerca sin pagar el mundo
  entero.
- **Jugador simulado** (`avatar.hpp`): un avatar con IA que decide por necesidades
  (`intent_for`), se mueve/come (`player_step`) y expone un resumen de lo que percibe y de la
  carga (`player_view`: observed/realized/abstract/dormant). Permite validar en host que la
  experiencia "se siente viva" y que el coste se concentra alrededor del jugador.

Estos cambios se verifican en HOST-174 y mantienen verdes los 23 tests de `eng::sim`. La
demo Amiga con render (consumidor real en `games/`) queda como siguiente paso: en este entorno
no se puede ejecutar WinUAE de forma fiable, así que no se presenta como verificado.

