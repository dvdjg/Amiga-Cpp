# Qué es el engine

Este repositorio contiene un **engine de juegos retro** escrito en **C++23** para correr en **Amiga
OCS/ECS/AGA** (objetivo primero: **A500**), con conectores previstos para otras plataformas (Atari ST,
Megadrive). No es un framework genérico: es un engine que **deja al juego escribir código de juego** y
baja al metal solo cuando hace falta.

## Objetivo

- **Un frame por VBlank** (50 Hz PAL) en el hardware objetivo, con **abstracciones de coste cero**:
  una llamada de la fachada no debe costar ciclos por sí misma en el camino caliente.
- **Portable por dentro, específico por fuera**: la lógica no nombra registros; el *backend* de la
  plataforma los escribe. Un juego portable usa solo la fachada (`eng/api/api.hpp`); una app Amiga
  añade su backend en `main()`.
- **Sin sorpresas de coste**: memoria **estática/arenas** (sin `malloc` en gameplay), sin excepciones,
  sin RTTI, **68000 puro** (nada de `68020`/FPU: lo verifica `tools/analyze/asm-audit.mjs`).

## Alcance

```
juego  ──►  fachada (eng/api)  ──►  motor (field/scene/graphics/…)  ──►  backend (platform/amiga)
   nivel A                      nivel B (Device)                         nivel C (metal)
```

El engine cubre: bucle y contrato de juego, escena y composición (Copper), dibujo planar y por Blitter,
**música/SFX (Paula)**, **mini-SO de mensajes** (E/S asíncrona, timers, tareas), **recursos**
(VFS, caché de assets, código dinámico HUNK/`.englib`, compresión ZX0), **UI**, y bibliotecas de
**matemáticas** genéricas, **simulación** e **IA** (steering, navegación, GOAP, motores de tablero y de
naipes).

## Estado

El engine es **evolutivo**: la fachada cubre lo que existe y crece con cada módulo (ver el checklist de
cobertura del [manual](../README.md)). Un módulo puede estar **implementado**, **parcial** o **solo
diseñado** (en `docs/engine/architecture/`); el manual marca el estado. La verdad operativa del código
está en `engine/include/eng/` — y toda referencia del manual **cita `fichero:línea`**.

## Para empezar

1. Este capítulo (filosofía, mapa de módulos, capas, glosario).
2. [Tutorial 01 — Hola mundo](../01_tutoriales/01_hola_mundo.md).
3. Build/run/depuración: `docs/build/BUILD_AND_RUN.md`.

Volver al [índice de introducción](README.md) · [índice del manual](../README.md).
