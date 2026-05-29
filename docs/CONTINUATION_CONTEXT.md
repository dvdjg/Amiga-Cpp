# Contexto para continuar desde cero

Si una IA abre este proyecto sin historial de conversacion, debe empezar leyendo:

1. `docs/DEVELOPMENT_LOG.md`
2. `docs/ROADMAP_ENGINE_CPP_AMIGA500.md`
3. `docs/BUILD_AND_RUN.md`
4. `docs/CODING_STYLE.md`
5. `docs/HARDWARE_AND_ROM_KERNEL_POLICY.md`
6. `demos/000_toolchain_cpp23/README.md`

## Objetivo inmediato

Mantener una base de engine C++23 verificable para Amiga 500. Cada cambio debe poder
probarse con:

```powershell
.\tools\test-regression.ps1
```

## Restricciones importantes

- No romper el proyecto C historico de la raiz.
- No asumir libc/STL hosted completa.
- No usar asignacion dinamica durante gameplay salvo pruebas controladas.
- No depender de Amiga en la logica de juego de alto nivel.
- Conservar evidencias de ejecucion en `out\run` y `out\regression`.
- Comentar cada unidad de codigo como tutorial, sobre todo cabeceras compartidas.
- Usar el Hardware Reference Manual local como referencia para registros y timing.
- Mantener el uso del ROM kernel como politica opcional de backend, no como detalle
  mezclado en la logica de juego.

## Estado minimo saludable

La demo `000_toolchain_cpp23` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- generar captura PNG;
- superar `analyze-demo.ps1`;
- mostrar `Memory arenas: OK` en el overlay.

La demo `010_chip_slow_memory` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- mostrar `Arena checks: OK`;
- mostrar barras para Chip, Slow y Frame;
- mostrar una base Chip en rango bajo y una base Slow en zona trapdoor/bogo cuando
  el perfil emulado expone memoria no-chip.
