# Contexto para continuar (puntero)

Este documento es un **puntero estable**, no un estado vivo: el estado del proyecto cambia cada semana y una copia aquí solo genera instrucciones obsoletas. Un hilo nuevo (IA o persona) empieza por [AGENTS.md](../AGENTS.md) — reglas operativas, router de tareas y orden de lectura — y continúa por [DOC-MAP-PRINCIPAL.md](ai-dev-environment/DOC-MAP-PRINCIPAL.md) (mapa «voy a hacer X → documentación»).

## Estado vigente (fuentes únicas)

- **Engine y demos (estado y próximas direcciones)**: [ROADMAP_UNIFICADO.md](guides/roadmap/ROADMAP_UNIFICADO.md).
- **Bloqueos e investigaciones abiertas**: [debugging/investigaciones/README.md](debugging/investigaciones/README.md), sección «Abiertas».
- **Deuda técnica con síntoma, evidencia y plan**: [ROADMAP_DEUDA_TECNICA.md](guides/roadmap/ROADMAP_DEUDA_TECNICA.md).
- **Auditoría viva de demos** (a eliminar/mejorar): [DEMOS_A_ELIMINAR_O_MEJORAR.md](guides/roadmap/DEMOS_A_ELIMINAR_O_MEJORAR.md).
- **Estado de cada roadmap** (activo/pausado/cerrado): [guides/roadmap/README.md](guides/roadmap/README.md).

## Operación diaria

- **Crear o arreglar demos/juegos**: [PROCEDIMIENTO_DEMOS_Y_JUEGOS.md](guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md).
- **Build/run/analyze y entorno local**: [build/BUILD_AND_RUN.md](build/BUILD_AND_RUN.md) y [ai-dev-environment/AMIGA_DEV_GUIDE.md](ai-dev-environment/AMIGA_DEV_GUIDE.md).
- **Validación visual (Ollama) y depuración visual**: [DEMO_VISUAL_DEBUG.md](guides/methodology/DEMO_VISUAL_DEBUG.md).

## Restricciones permanentes

Las siguientes restricciones no dependen del estado del proyecto y siguen vigentes:

- No romper ni mezclar el proyecto C histórico de `legacy/` con el flujo del engine C++23.
- Sin libc/STL hosted completa; sin asignación dinámica durante gameplay.
- La lógica de juego no depende de Amiga: Amiga es un backend.
- Evidencia reproducible en `out/` (gitignored); lo canónico de texto, a `docs/` o `artifacts/`.
