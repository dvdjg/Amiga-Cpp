# HOST-149: explicación en lenguaje natural (NLG)

Test host de `eng/board/explain/{explain,templates}.hpp`.

## Qué enseña / comprueba

Generación por **plantillas + reglas**, sin modelo de lenguaje: se extraen los
rasgos (los mismos de la evaluación) y se eligen 2–4 frases preescritas ES/EN.

```
rasgos + evaluación  ->  reglas de prioridad  ->  plantillas  ->  párrafo
```

Reglas validadas: jaque inmediato, ventaja material, dama prematura, retraso de
desarrollo y rey en el centro; tono enfático por gran desequilibrio (`!`); conectores
entre frases; y escritura con truncado seguro (nunca desborda el buffer).

## Salida de referencia

```
Ajedrez: explicacion:
OK: NLG (dama prematura, material, jaque, tono y truncado) ES/EN
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/149_chess_explain
```
