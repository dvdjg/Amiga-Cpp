# HOST-173: laboratorio de escenarios (`eng::sim`)

No es un test de aserciones duras: es una **simulación larga** de varios escenarios
iniciales que imprime un **digest** de la evolución (población, nacimientos y muertes por
causa, medias de necesidades, histograma de conductas). Sirve para que la IA (o un humano)
**observe el sistema a lo largo del tiempo y ajuste parámetros** hasta que se comporte como
un mundo vivo.

## Qué hace

- Cuatro escenarios: `abundante`, `escaso`, `depredadores` y `manada`.
- 4000 ticks por escenario con un digesto cada 500.
- Reglas de juego mínimas en el arnés (percepción, caza, comer del bioma, cortejo,
  reproducción con gestación, para que el engine se ejercite completo).
- Invariantes suaves: hubo población, **natalidad** y **mortalidad** (por hambre, vejez o
  depredación) y no se superó la capacidad.

El valor está en el digesto: análisis de los ajustes y analogías con el mundo real en
`docs/debugging/investigaciones/sim-ecosystem-scenarios.md`.

## Salida de referencia (resumen)

```
[abundante] t=4000 alive=64 births=432 kills=3 deaths H=2 A=63 P=3 ...
totales: nacimientos=1334 muertes=215 poblacion minima final=2
OK: Sim scenarios (mundo viable: natalidad y mortalidad observadas)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/173_sim_scenarios
```
