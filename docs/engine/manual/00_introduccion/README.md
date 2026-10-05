# Introducción

Puerta de entrada al engine: qué es, su filosofía y el mapa de módulos y capas. **Léelo antes que
nada**; el resto del manual asume este vocabulario.

| Página | Contenido |
|---|---|
| 01_que_es_el_engine.md | Objetivo (juegos retro en Amiga OCS/ECS/AGA, A500 primero), alcance y estado. |
| 02_filosofia_y_reglas.md | Abstracciones de **coste cero**, «la app pide / el engine dispone», sin heap/RTTI/excepciones, 68000 puro. |
| 03_mapa_de_modulos.md | Los **23 módulos** (`api`, `core`, `graphics`, `field`, `scene`, `audio`, `ai`, `board`, …) y sus dependencias (ASCII). |
| 04_arquitectura_de_capas.md | Capas: `core` → `memory`/`graphics` → `field`/`scene` → `api` → `platform` (ASCII). |
| 05_glosario.md | Vocabulario de dominio: `App`, `Screen`, `Scene`, `Layer`, `Actor`, `ScrollPlan`, `Tag`, `Block`, … |

Volver al [índice del manual](../README.md).
