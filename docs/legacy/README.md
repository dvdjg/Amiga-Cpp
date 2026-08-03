# Notas históricas (cuarentena)

> **Procedencia y aviso:** estas notas proceden del repo hermano `Cursor-Amiga-C` y son
> registros antiguos de troubleshooting (febrero de 2026) sobre el sistema de depuración,
> escritos con ortografía irregular y muy solapados entre sí. Se conservan **tal cual** como
> historial, a la espera de una fusión/limpieza con la documentación canónica de
> [../debugging/](../debugging/README.md). No tomar decisiones de diseño a partir de ellas sin
> contrastar primero con la documentación vigente.

| Documento | Tema |
|-----------|------|
| [bug-breakpoints-no-funcionan.md](bug-breakpoints-no-funcionan.md) | Breakpoints C que no se activan (`baseText=0` vía qOffsets). |
| [CONTEXTO-DEPURACION-C.md](CONTEXTO-DEPURACION-C.md) | Contexto de conversación para retomar el bug de breakpoints. |
| [NUEVO-CHAT-CONTEXTO.md](NUEVO-CHAT-CONTEXTO.md) | Nota de contexto alternativa para nueva conversación (mismo bug). |
| [WORKAROUND-BREAKPOINTS.md](WORKAROUND-BREAKPOINTS.md) | Workaround de breakpoints por relocalización (estado: NO FUNCIONA). |

## Pendiente

- Fusionar el contenido aún válido con [../debugging/](../debugging/README.md)
  (RELOCATION-FIX, DEBUGGING-ARCHITECTURE, HISTORIAL-CAMBIOS).
- Corregir la ortografía del texto que se conserve y descartar lo obsoleto.
- Eliminar esta carpeta cuando la fusión esté completa.
