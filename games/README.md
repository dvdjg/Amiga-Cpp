# `games/` — juegos generados con el engine

El engine está en continua evolución; los **primeros juegos no se hacen en
repositorios separados**, sino en este repo bajo `games/`, para iterar engine +
juego juntos. Cuando un juego madure podrá extraerse a su propio repositorio.

La especificación completa está en `docs/STRUCTURE.md` (§9).

## Estructura

```
games/
├── README.md                  → este fichero: qué juegos hay, estado, mecánicas
└── NNN_nombre/                → mismo esqueleto que una demo
    ├── src/                   → código C++ del juego (main.cpp + unidades)
    ├── README.md              → qué es, controles, estado, cómo build/run/analyze
    ├── analyze-sequence.sh    → (opcional) secuencia de verificación
    └── …                      → analizadores propios (como en demos/)
```

## Reglas

- Se invoca con las mismas tools que las demos:
  `bash tools/build/build-demo.sh games/NNN_nombre --debug --clean`,
  `bash tools/run/run-demo.sh games/NNN_nombre`, etc.
- Los assets propios del juego: fuente en `assets/<plataforma>/…`, generados en
  `out/assets/<juego>/…`, igual que en las demos.
- Cada juego documenta en su `README.md` qué mecánicas nuevas del engine fuerza
  (y por tanto qué cambios de motor trae), porque los primeros juegos descubren
  el engine.
- Cambios al engine que un juego exige se describen en
  `docs/guides/roadmap/ROADMAP_UNIFICADO.md` y en el README del juego.