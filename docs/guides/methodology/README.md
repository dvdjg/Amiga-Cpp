# Metodología y procesos

Metodología de desarrollo, runbooks de agentes IA, prompts y automatización. Esta carpeta
agrupa los procesos de trabajo, no el diseño del engine.

> **Procedencia:** la mayoría de estos documentos proceden del repo hermano `Cursor-Amiga-C`
> y se incorporan aquí por tema. El registro de desarrollo es propio de este repositorio.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md) | Registro de desarrollo del engine actual (este repo): decisiones, descubrimientos y comandos verificados. |
| [development-methodology.md](development-methodology.md) | Metodología incremental general: fases verificables, no big-bang. |
| [agent-runbook.md](agent-runbook.md) | Runbook del agente IA: compilación, lanzamiento/depuración WinUAE, análisis y escalado al usuario. |
| [agent-system-roadmap.md](agent-system-roadmap.md) | Sistema de agentes G0-G5: roles, cola de trabajo y Definition of Done. |
| [agents/](agents/README.md) | Puntero al sistema de agentes (prompts y skills). |
| [amiga-lowlevel-agent-prompt.md](amiga-lowlevel-agent-prompt.md) | Prompt maestro para trabajo close-to-the-metal. |
| [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md) | Plantilla de contrato técnico por fases. |
| [AI-AUTONOMY-IDEAS.md](AI-AUTONOMY-IDEAS.md) | Ideas para dar autonomía visual a la IA. |
| [cursor-sigue-automation-spec.md](cursor-sigue-automation-spec.md) | Especificación de automatización de "sigue" en Cursor. |
| [comfyui-integration.md](comfyui-integration.md) | Integración ComfyUI text-to-image (herramienta auxiliar). |
| [SKILL-amiga-engine-lowlevel.md](SKILL-amiga-engine-lowlevel.md) | Skill de agente para trabajo low-level Amiga (de `Cursor-Amiga-C`). |

## Regla de idioma

Según [AGENTS.md](../../AGENTS.md), toda la documentación en español debe usar ortografía
correcta (tildes, eñes y puntuación). Los documentos nuevos deben cumplirla desde el primer
commit.
