# HOST-426 — filtro Bloom en GOAP

Valida `eng::util::BloomFilter` y su integración opcional en `eng::ai::Goap::Planner`.

## Qué cubre

- No hay falsos negativos: cada clave insertada siempre da `may_contain == true`.
- Un positivo Bloom falso no altera la decisión porque el planner confirma en `m_best`.
- Comparación sin filtro / con filtro: mismo plan, coste, estado final y expansiones.
- Comparación de filtros de 32 y 256 bits: ambos conservan el resultado; se informan consultas exactas evitadas y falsos positivos observados.
- Un filtro con hash constante fuerza un positivo falso reproducible, que se confirma con la pertenencia exacta.
- El parámetro por defecto conserva desactivado el filtro.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/ai/426_goap_bloom
```
