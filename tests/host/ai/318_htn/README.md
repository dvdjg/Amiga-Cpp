# HOST-318: planificación jerárquica HTN (`eng::ai::Htn`)

Test host de `engine/include/eng/ai/planning/htn.hpp`. A diferencia del GOAP (que busca con
A* la secuencia de coste mínimo), el HTN **descompone** una tarea compuesta en subtareas por
**métodos** (precondición → lista de subtareas) y comprueba que cada acción primitiva sea
aplicable en el estado que va resultando. Es la descomposición clásica con **backtracking
acotado** (`MaxDepth`, `MaxPlan`), determinista y sin heap. Reutiliza `Goap`'s `State`/
`Action` y `applicable`/`apply`.

## Qué comprueba

1. **Mecanismo**: un dominio de pastel (`comprar → batir → hornear`) se descompone en el
   orden correcto.
2. **Métodos con precondición**: con la mezcla ya hecha, el primer método (solo hornear)
   basta.
3. **Fallo controlado**: si ningún método descompone, devuelve `0`.
4. **Consumidor real** (`domain.hpp::build_shelter_htn`): «conseguir refugio» devuelve la
   cadena `Gather → CraftTool → Gather → Build`, **el mismo plan** que obtiene el GOAP del
   mismo dominio; con materiales y herramienta previos basta `Build`.

## Salida de referencia

```
HTN:
OK: HTN (descomposicion, metodos con precondicion, fallo y equivalencia con GOAP)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/318_htn
```
