# Doc-map principal (IA → documentación)

**Archivo de navegación obligatorio para la IA antes de tocar el engine o las demos.**

Este documento es la puerta de entrada única: ante cualquier tarea de desarrollo,
efecto o consulta de hardware, la IA debe **leer primero este mapa** para saber
qué documentación consultar, qué demos/APIs ya existen y qué reglas aplicar.
Cuando termine la lectura contextual, puede abrir solo los enlaces que necesite.

> Regla de oro: **buscar antes de crear.** Si la técnica, el efecto o la
> referencia ya está documentado, consiste en *reutilizar/extender/componer*,
> nunca en duplicar. Solo cuando no exista nada equivalente se crea contenido
> nuevo, y siempre en el sitio canónico (ver [STRUCTURE.md](../../STRUCTURE.md)).

---

## 1. Protocolo previo a cualquier tarea

1. Si es la **primera vez** en el repo en esta sesión, leer `AGENTS.md` y este
   doc-map.
2. Identificar el **tipo de tarea** en la tabla §3 y abrir los documentos de su
   fila antes de escribir código.
3. Si la tarea es **un efecto** o **una técnica**, consultar §4 (efecto → demo/API)
   y comprobar si ya hay cobertura (estado de la fila en el coverage index).
4. Si la tarea requiere **referencia oficial de hardware**, ir a §5 (AHRM 3.ª edición + fuentes autoritativas).
5. Si la tarea viene de un **repositorio o directorio externo** (nuevo efecto,
   referencia, demoscene-repo, etc.), aplicar el protocolo de ingesta de §6.
6. **Después** de implementar: documentar (sitio canónico), enlazar aquí y en
   `docs/README.md`, y registrar estado en el coverage/roadmap correspondiente.
   Seguir la regla de evidencia de `AGENTS.md` (build → run → analyze).

---

## 2. Lectura contextual mínima (según profundidad)

| Si necesito... | Leer |
|---|---|
| Contexto global del proyecto | [docs/README.md](../README.md), [CONTINUATION_CONTEXT.md](../CONTINUATION_CONTEXT.md) |
| Estructura del repo (dónde va cada cosa) | [docs/STRUCTURE.md](../STRUCTURE.md) |
| Arquitectura del engine C++ | [docs/engine/architecture/](../engine/README.md) y sus subcarpetas |
| Estilo/restricciones del engine | [CODING_STYLE.md](../engine/architecture/CODING_STYLE.md), [HARDWARE_AND_ROM_KERNEL_POLICY.md](../engine/architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md) |
| Contrato de bajo nivel Amiga (contrato técnico) | [amiga-lowlevel-agent-prompt.md](../guides/methodology/amiga-lowlevel-agent-prompt.md) y [amiga-lowlevel-technique-contract-template.md](../guides/methodology/amiga-lowlevel-technique-contract-template.md) |
| Bucles de entrada/backend | [engine.hpp](../../../engine/include/eng/engine.hpp), `amiga_minimal.cpp` (ver AGENTS.md §Rutas) |
| Build/run/analyze | [BUILD_AND_RUN.md](../build/BUILD_AND_RUN.md) |
| Depuración WinUAE/MCP | [DEBUG-WINUAE-V2-GUIDE.md](../debugging/DEBUG-WINUAE-V2-GUIDE.md) |
| Evidencia / visual | [session-evidence.md](session-evidence.md), [testing/](../testing/README.md) |

---

## 3. Tabla de tareas → documentación

| Tipo de tarea | Documentos a consultar primero | Demos/APIs de referencia |
|---|---|---|
| **Nuevo efecto demoscene** | §4 de este mapa, [DEMOSCENE_EFFECT_REPLICATION_POLICY.md](../demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md), [demoscene-repo-coverage-index.md](../demos/effects/demoscene-repo-coverage-index.md), [demoscene-repo-import-roadmap.md](../demos/effects/demoscene-repo-import-roadmap.md), §5 para registros | `engine/` y `demos/` de la técnica base más cercana |
| **Scroll / tiles por playfield** | [DEMOSCENE_REPO_INDEX.md](../demos/effects/DEMOSCENE_REPO_INDEX.md) (tiles16), técnicas de scroll, §5 (BPLCON1/BPLxPT) | `engine/include/eng/graphics/drivers/tile_scroll.hpp`, demos 100–107, 201–202 |
| **Copper / paleta / per-line** | [amiga-a500-dma-copper-state-rules.md](../reference/amiga/hardware/amiga-a500-dma-copper-state-rules.md), técnicas `copper-chunky.md`, `dx39...analysis.md` | `engine/.../copper/`, demos 020, 030, 040 |
| **Blitter / BOBs / minterms** | [DEMOSCENE_REPO_INDEX.md](../demos/effects/DEMOSCENE_REPO_INDEX.md) (blitter interleaved), técnicas `dual-playfield-fastbobs.md`, AHRM cap. 6 | demos 050, 051, 052, 200s |
| **Dual playfield / prioridad** | `dual-layer.md`, DPF_MIXTO_SPLIT_LINEAL.md, dx39-layers analysis | demos 102, 104, 105, 106, 107, 202 |
| **Sprites / overlays** | `sprite-layer.md`, AHRM cap. 4 | — (ver coverage) |
| **Audio / Paula** | `audio-mixing.md`, AHRM cap. 5 | — |
| **3D fixed-point / wireframe** | DEMOSCENE §lib3d, técnicas genéricas | — |
| **Pipeline tiles/EHB/assets** | [PIPELINE_TILES_EHB.md](../demos/tile-pipeline/PIPELINE_TILES_EHB.md), [REGLAS_PIPELINE_TILES.md](../guides/roadmap/REGLAS_PIPELINE_TILES.md), `tools/amiga-tiles/README.md` | demos 201, 202 |
| **Juego sobre el engine** | [STRUCTURE §9](../../STRUCTURE.md), roadmap, técnicas | `games/` |
| **Optimización de un path** | Regla permanente de rendimiento de `AGENTS.md`, perfilado (`tools/profile/README.md`) | — |
| **Depurar un bug de visual** | [DEBUG-WINUAE-V2-GUIDE.md](../debugging/DEBUG-WINUAE-V2-GUIDE.md), invariantes microtests | — |
| **Nueva referencia/documento externo** | §6 de este mapa, [amiga-authoritative-sources.md](../reference/amiga-authoritative-sources.md) | — |

---

## 4. Efectos demoscene → demo/API/estado

**Índice efecto a efecto (estado local, caso batería y API candidata):**
[demoscene-repo-coverage-index.md](../demos/effects/demoscene-repo-coverage-index.md).

**Lectura recomendada por dominio:**

| Dominio | Efectos del catálogo (01–67) | Docs del repo |
|---|---|---|
| Copper por línea / bandas | 04-plasma, 08-floor, 12-stripes, 50-roller, 53-showpchg | `copper-chunky.md`, `dx39` layers, AHRM cap. 2 |
| Playfield / dual / HAM | 39-layers, 58-tiles8, 59-tiles16, 60-tilezoomer, 61-transparency | `dual-layer.md`, técnicas scrolling |
| Sprites | 13-highway (sprites+playfields) | `sprite-layer.md` |
| Blitter creativo | 11-game-of-life, 67-weave, 14-metaballs | AHRM cap. 6 (minterms/línea) |
| 3D | 06-wireframe, 30-flatshade, 55-stencil3d, 56-texobj, 65-uvmap | `demoscene-repo` §lib3d |
| Audio | 44-playahx, 45-playcinter, 46-playp61, 47-playpt | `audio-mixing.md` |
| Texto / UI | 09-textscroll, 27-credits, 37-gui | Texto vía API: `Surface::draw_text`/`draw_text5` (`eng/field/surface.hpp`, UTF-8) + `Font8` (LATIN-1, `eng/graphics/font8.hpp`) y `Font5x7` (HUD compacto, `eng/graphics/font5x7.hpp`); decodificador mínimo `eng/core/utf8.hpp`. Regla: no reimplementar `draw_text` — usar estas utilidades. |

**Demos propias del engine que ya cubren técnicas base** (leer su README para
invariantes y comandos de validación): `000`… `052` (toolchain/copper/blitter/
tiles) y `100`–`107` (scroll/tile-field/dual-playfield/x-limited) y `201`–`202`
(EHB mapa/DPF). El roadmap general (estado y próximas direcciones) está en
[ROADMAP_UNIFICADO.md](../guides/roadmap/ROADMAP_UNIFICADO.md).

---

## 5. Referencia oficial de hardware

| Fuente | Dónde | Uso |
|---|---|---|
| **AHRM 3.ª edición (1990)** | [ahrm/README.md](../reference/ahrm/README.md) + [índice](../reference/ahrm/amiga-hardware-manual-index.md) + texto `.cat.md` | Consultar **registros/bits**, mecanismos (sprite, blitter, copper, audio) y comportamiento ECS/AGA/A3000. |
| **Fuentes autoritativas y reglas** | [amiga-authoritative-sources.md](../reference/amiga-authoritative-sources.md) | Qué fuente es primaria, cuáles didácticas, cuándo contrastar. |
| **Reglas reutilizables A500** | [amiga-a500-dma-copper-state-rules.md](../reference/amiga/hardware/amiga-a500-dma-copper-state-rules.md) | Invariantes DMA/copper/scroll probadas. |
| **Matriz chipsets** | [amiga-chipset-matrix.md](../reference/amiga/hardware/amiga-chipset-matrix.md) | OCS/ECS/AGA: qué registros revisar primero. |
| **Chipset invariantes microtests** | [amiga-hardware-invariants-microtests.md](../reference/amiga/hardware/amiga-hardware-invariants-microtests.md) | Microtests MI01–MI08. |

---

## 6. Protocolo de ingesta de repos/referencias externas

Regla general: **conservar e indexar solo la mejor información.** No se mantienen
duplicados; si una fuente dice lo mismo que otra, prevalece la mejor y la otra se
descarta (o se integra su contenido no repetido si aporta algo distinto).

Cuando se aporta un **repositorio, directorio o documento externo** (por ejemplo
`demoscene-repo/effects/…`, un manual OCR, un repo de demos/engine…), seguir este
orden:

1. **Buscar primero lo que ya existe** en este repo:
   - ¿Ya está documentado un efecto/ técnica equivalente? Consulta §4 y el
     coverage-index. ¿Ya existe la referencia (AHRM ediciones, fuentes
     autoritativas)? Consulta §5.
2. **Evaluar la calidad** de lo nuevo frente a lo existente:
   - Comparar profundidad, exactitud y utilidad para el engine (caso real).
   - Criterio: lo nuevo **supera**, **complementa** o **repite** lo ya presente.
3. **Decidir el destino**, según el resultado:
   - **Sustituir**: solo si lo nuevo es claramente superior y lo viejo es obsoleto
     o erróneo. Mover el anterior a `legacy/` o históricos con banner, y anotar la
     decisión.
   - **Componer/fusionar**: si cada fuente aporta algo distinto y no duplica (p. ej.
     un manual completo + una ficha técnica resumen). El contenido repetido siempre
     se descarta; no se mantienen dos documentos que digan lo mismo.
   - **Añadir como referencia**: si es material de consulta que no contradice ni
     duplica → carpeta canónica de `docs/reference/<plataforma>/…`.
   - **Catálogo de efecto**: añadir fila en el coverage-index y abrir ficha en la
     demo si se replica.
   - **Descartar/anotar**: si es inferior o duplicado, dejar una nota de decisión
     en `amiga-authoritative-sources.md` (o README de la carpeta) explicando por
     qué no se incorporó.
4. **No copiar binarios/media**: volcar solo texto/markdown; la media va a `out/`
   (gitignored). Los assets *fuente* van a `assets/<plataforma>/<dominio>/`.
5. Respeta el formato de docs (word wrap, español, diagramas ASCII) y la regla de
   evidencia. La decisión de ingesta (sustitución/descarte) se refleja en qué
   queda en el repo; no hace falta una nota de proceso en el documento de
   referencia.

---

## 7. Enlaces rápidos transversales

- Índice maestro de docs: [docs/README.md](../README.md)
- Roadmap vigente: [ROADMAP_UNIFICADO.md](../guides/roadmap/ROADMAP_UNIFICADO.md)
- Coverage efecto → estado: [demoscene-repo-coverage-index.md](../demos/effects/demoscene-repo-coverage-index.md)
- Fuentes autoritativas: [amiga-authoritative-sources.md](../reference/amiga-authoritative-sources.md)
- Entorno IA (Ollama, sesión, pruebas): [ai-dev-environment/README.md](README.md)