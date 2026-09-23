# HOST-162: atención y sentidos por genética (`eng::sim`)

Test host de la **atención** (percepción↔conducta) y de la expresión del genoma en los
sentidos.

## Qué comprueba

1. **`focused`**: el **miedo** estrecha el cono visual (con suelo `min_arc`) y acorta la
   vista, pero **agudiza el oído** (hipervigilancia) y reduce el olfato; la **ira** enfoca
   (sube la agudeza). Sin miedo, los sentidos no cambian.
2. **`senses_from_genome`**: más velocidad → mejor vista y, por encima del umbral,
   **ecolocalización**; la sociabilidad da oído y el tamaño, olfato.
3. **Ecolocalización**: el oído cruza a la región contigua casi sin penalización.
4. **Atención en la conducta** (`attention_score`/`best_attention_tracker`): un objetivo
   detectado por **varios sentidos** y saliente pesa más que una detección fuerte pero
   monomodal; es lo que elige a qué prestar atención la decisión.

## Salida de referencia

```
Sim attention:
OK: Sim attention (foco, sentidos por genoma, atencion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/162_sim_attention
```
