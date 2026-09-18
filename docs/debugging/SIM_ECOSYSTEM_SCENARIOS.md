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
- El reciclado de capacidad obliga a acotar la población por escenario; un modelo de
  **capacidad por región** (aforo) sería más fiel que un tope global.
- La caza es por contacto; sin "emboscada" ni persecución larga, los depredadores dependen del
  azar del encuentro. La coordinación de manada mejora la captura (HOST-172) y es la vía para
  tácticas más ricas.
