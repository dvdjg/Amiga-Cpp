# Contrato de equivalencia entre plataformas (features)

Una **feature** debe comportarse **igual** en todas sus variantes de plataforma: la **misma
secuencia de decisiones** produce el **mismo resultado lógico**. Como la lógica vive en el engine
(anillo 0) y cada plataforma es un adaptador fino, la equivalencia se puede **verificar en host**,
sin emulador.

## Qué se compara (y qué no)

- **Sí** (lógico, determinista): reglas, estado, decisiones y resultados de la simulación (quién
  gana, secuencia de jugadas, valor de una mano, estado de un widget tras una entrada).
- **No** (presentación): número de blits, paleta, orden de dibujo, fps. Eso es del adaptador y se
  valida por demo (`analyze-sequence.sh`, pixel asserts).

## Cómo se garantiza

1. **La lógica no toca hardware.** Gate `tools/check/demo-platform-boundaries.mjs`: `demos/features/**`
   no usa registros ni vocabulario de chipset.
2. **La lógica está en el engine y tiene test host determinista.** El test host del módulo del
   engine **es** el fixture de paridad: fija una entrada y asevera la salida. Un adaptador que
   cambie el resultado rompe el test.
3. **Convenio de escenario canónico.** El README de cada feature documenta el escenario que se usa
   como referencia (semilla, entradas) y enlaza su test host.

## Fixtures actuales (por feature)

| Feature | Módulo engine | Fixture host (referencia) |
|---|---|---|
| `engine` | `eng::core` | HOST-000 (`eng_core_math`), HOST-073…092 (util) |
| `board/chess` | `eng::board` | HOST-138…151 (movegen/perft, búsqueda, eval, storage) |
| `cards` | `eng::cards` | HOST-188…198 (rules/equity/selfplay) + `tools/cards/regression.sh` (barrido congelado) |
| `ui` | `eng::ui` | HOST-223…230 (painter, widgets, foco, layout, ventanas, compositor) |

## Cómo añadir una variante de plataforma

1. Implementar el adaptador `features/<feature>/<plataforma>/NNN_<tema>/` (solo backend, assets y
   mapping de entrada; la lógica no cambia).
2. El **fixture host debe seguir verde sin tocarlo** (si hay que tocarlo, la variante cambió la
   lógica → subir el cambio al engine y revalidar todas las variantes).
3. Añadir la fila en la matriz de paridad de [`README.md`](README.md) y, si aplica, `vision-points.json`
   para los puntos de transición.

> Base para validar la paridad cuando existan varias plataformas: los fixtures son deterministas y
> corren en host; el mismo escenario se podrá reproducir con cada backend y comparar el resultado
> lógico.
