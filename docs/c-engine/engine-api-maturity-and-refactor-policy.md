# Politica de madurez y refactor del engine

Este documento fija una regla simple para la evolucion del engine:

- primero incorporar capacidad funcional y demostrada;
- despues refactorizar hacia APIs mas genericas, modulares y reutilizables cuando aparezcan patrones reales;
- no congelar como definitiva la primera forma en que una tecnica entra al engine.

## Idea central

En este proyecto el engine se alimenta de:

- casos de bateria;
- efectos importados desde `demoscene-repo`;
- tecnicas probadas en WinUAE con evidencia real.

Por tanto, muchas APIs nacen primero como una extraccion minima y funcional. Eso es aceptable y deseable siempre que:

- la capacidad quede demostrada;
- el comportamiento sea verificable;
- la documentacion deje claro si la API ya esta madura o aun es provisional.

## Regla de integracion

Cuando una tecnica nueva entra al engine, la pregunta principal no es:

- "es esta la abstraccion final perfecta?"

La pregunta correcta es:

- "aporta ya una capacidad reusable real y demostrada?"

Si la respuesta es si, puede entrar al engine aunque su forma aun no sea la definitiva.

## Regla de refactor

Una vez incorporada una tecnica reusable, debemos estar dispuestos a refactorizarla si ocurre al menos uno de estos casos:

1. aparece un segundo o tercer consumidor con necesidades parecidas pero no identicas;
2. se detecta que la API mezcla responsabilidades low-level y retained;
3. una implementacion funcional impide especializacion por bitplanes, layout, clipping, mascara o ownership;
4. el codigo reusable queda demasiado acoplado al primer caso que lo promovio;
5. la bateria demuestra que la API actual obliga a duplicar logica o a inventar workarounds.

## Etiquetas de madurez recomendadas

### `PROVISIONAL_REUSABLE`

La API ya sirve y ya tiene valor real, pero todavia puede cambiar de forma.

### `STABLE_REUSABLE`

La API ya ha demostrado convergencia suficiente.

### `CASE_LOCAL_ONLY`

La tecnica sigue siendo demasiado especifica y no merece promotion aun.

## Disciplina recomendada

- no bloquear una capacidad demostrada esperando una abstraccion perfecta;
- generalizar cuando dos o mas casos reales revelan el patron;
- refactorizar solo con evidencia preservada;
- separar primitive low-level y wrapper retained cuando la API empiece a esconder costes o ownership distintos.

## Estado actual de ejemplo

### `engine_palette_cycle_*`

Estado recomendado actual:

- `PROVISIONAL_REUSABLE`

Motivo:

- ya encapsula una tecnica autentica y barata del Amiga;
- ya tiene un caso consumidor real (`DX03`);
- todavia faltan mas efectos importados para decidir su forma final.

### `engine_copper_set_contiguous_planes`

Estado recomendado actual:

- `STABLE_REUSABLE`

Motivo:

- expresa un contrato bajo nivel concreto y robusto;
- resuelve una necesidad clara de layouts planares no interleaved;
- es poco probable que su semantica base cambie aunque aparezcan mas consumidores.

## Regla operativa para futuras sesiones

Cuando se promueva una tecnica desde bateria o desde `demoscene-repo`, dejar siempre documentado:

1. si entra al engine como `PROVISIONAL_REUSABLE` o `STABLE_REUSABLE`;
2. que parte exacta sigue siendo local al caso;
3. que evidencias sostienen la promotion;
4. que condiciones dispararian un refactor posterior.

## Referencias relacionadas

- [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md)
- [engine-subsystems.md](engine-subsystems.md)
- [engine-roadmap.md](engine-roadmap.md)
- [demoscene-repo-import-roadmap.md](demoscene-repo-import-roadmap.md)
