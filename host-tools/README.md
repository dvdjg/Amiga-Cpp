# `host-tools/` — programas de apoyo independientes del engine

Programas completos que corren en la máquina de desarrollo **sin depender del
engine ni del toolchain Amiga**: utilidades Go o C++ compiladas para PC, bots,
convertidores, generadores, etc. No forman parte del pipeline de `tools/` (que
sí depende del repo y del toolchain Amiga); se compilan y ejecutan por separado.

La especificación completa de organización está en `docs/STRUCTURE.md` (§7).

## Estructura

```
host-tools/
├── README.md                  → este fichero
└── <programa>/                → cada programa en su propio subdirectorio
    ├── go.mod / CMakeLists.txt / Makefile / …
    ├── src/ o raíz del código
    ├── README.md              → qué hace, cómo se compila y cómo se ejecuta
    └── (salidas → out/tmp/<programa>/ o out/playground/<experimento>/)
```

## Reglas

- Independencia real: los programas de `host-tools/` no enlazan contra
  `engine/`, `dist/` ni requieren el toolchain Amiga.
- Cada programa lleva su propio `README.md` con los comandos de build/ejecución.
- Las salidas que generen siguen las reglas de `out/` (`out/tmp/…`,
  `out/playground/…`); nunca escriben en su propio directorio fuentes.
- Un programa que acabe necesitando el entorno de demos (runner, capturas)
  debería migrar a `tools/` — no al revés.