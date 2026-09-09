# Efectos demoscene

Importación y réplica de efectos del repositorio externo `demoscene-repo`, política de
replicación, índices de cobertura y análisis de efectos concretos.

> **Procedencia:** la política de replicación y el índice del repo externo son propios de
> este repositorio; los documentos de importación proceden del repo hermano `Cursor-Amiga-C`.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [DEMOSCENE_EFFECT_REPLICATION_POLICY.md](DEMOSCENE_EFFECT_REPLICATION_POLICY.md) | Política: no portar línea a línea, reconstruir con APIs limpias del engine, numeración de demos. |
| [DEMOSCENE_REPO_INDEX.md](DEMOSCENE_REPO_INDEX.md) | Índice técnico del repositorio externo `demoscene-repo` (estructura, patrones, catálogo). |
| [LIBRARIES-CPP23-IMPORT-ROADMAP.md](LIBRARIES-CPP23-IMPORT-ROADMAP.md) | **Roadmap vigente**: portar las librerías `lib*` de `demoscene-repo-orig` a C++23 en `engine/`, validadas efecto a efecto (por librerías, no por efectos). |
| [OLEADA1_LIBGFX_INVENTARIO.md](OLEADA1_LIBGFX_INVENTARIO.md) | Inventario y mapeo de la Oleada 1 (`libgfx`) contra la nueva estructura del engine: `CopListT`→`Scheduler+CopperIntent`, copiezas a portar (`c2p_1x1_4`, `CopWaitSafe`, `Bitmap`). |
| [CONTINUAR_INGESTA_DEMOSCENE.md](CONTINUAR_INGESTA_DEMOSCENE.md) | **Prompt de hilo nuevo** para continuar la ingesta: contexto, estado actual y patrón de importación con tests. |
| [demoscene-effects-integration.md](demoscene-effects-integration.md) | Cómo adaptar efectos al engine: catálogo técnica -> API, oleadas. |
| [demoscene-repo-import-roadmap.md](demoscene-repo-import-roadmap.md) | Roadmap anterior de importación por efectos (superado por el enfoque por librerías). |
| [demoscene-repo-coverage-index.md](demoscene-repo-coverage-index.md) | Índice efecto a efecto (01-67 + Starfox) del estado de importación. |
| [dx39-layers-original-analysis.md](dx39-layers-original-analysis.md) | Despiece técnico del efecto `layers` (dual playfield, scroll fino/coarse, wrap). |

## Enlaces relacionados

- Demos del engine que ya implementan estos efectos: `demos/` (README por demo).
- Técnicas de composición: [../../reference/amiga/techniques/](../../reference/amiga/techniques/README.md).
