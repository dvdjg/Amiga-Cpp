# Contexto para continuar desde cero

Si una IA abre este proyecto sin historial de conversacion, debe empezar leyendo:

1. `docs/DEVELOPMENT_LOG.md`
2. `docs/ROADMAP_ENGINE_CPP_AMIGA500.md`
3. `docs/BUILD_AND_RUN.md`
4. `docs/CODING_STYLE.md`
5. `docs/HARDWARE_AND_ROM_KERNEL_POLICY.md`
6. `docs/MOUSE_AUTOMATION.md`
7. `docs/WINUAE_SIDE_CHANNEL_DEBUG.md`
8. `demos/000_toolchain_cpp23/README.md`

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
- Durante pruebas automatizadas, WinUAE no debe capturar ni encerrar el raton de
  Windows. El runner fuerza `win32.absolute_mouse=yes` y las pruebas deben mover
  el raton emulado con `tools\input\mouse-path.ps1`.
- Las demos deben exponer `g_amg_run_status` y llegar a `Ready` por el canal
  lateral de WinUAE-DBG antes de la captura. El canal escucha en `127.0.0.1:2346`
  y el runner lo usa sin detener el 68000.
- La colaboracion profunda persona+IA sobre la misma instancia viva de WinUAE ya
  tiene canal lateral con `observe/assist/takeover`, debug lock y acciones seguras
  encoladas para `screenshot`, `input` y `profile`. Sigue pendiente la parte
  peligrosa: auditoria de escrituras, snapshots/rollback, pausa/reanudar por lock
  y zona scratch para diagnostico 68k. Ver `docs\WINUAE_SIDE_CHANNEL_DEBUG.md`.

## Estado minimo saludable

La demo `000_toolchain_cpp23` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- generar captura PNG;
- superar `analyze-demo.ps1`;
- mostrar `Memory arenas: OK` en el overlay.

La demo `010_chip_slow_memory` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar `Arena checks: OK`;
- mostrar barras para Chip, Slow y Frame;
- mostrar una base Chip en rango bajo y una base Slow en zona trapdoor/bogo cuando
  el perfil emulado expone memoria no-chip.

La demo `020_copper_basic` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- tomar el display a pantalla completa;
- mostrar bandas horizontales roja, verde, azul, amarilla y cian;
- superar su analizador especifico `demos\020_copper_basic\analyze-screenshot.ps1`.

La demo `030_ehb_palette_zones` debe:

- compilar en debug;
- ejecutarse en WinUAE-DBG;
- alcanzar `side-channel READY`;
- mostrar una reticula EHB con tres zonas verticales de paleta;
- incluir muestras visibles de colores normales 0..31 y half-brite 32..63;
- superar su analizador especifico `demos\030_ehb_palette_zones\analyze-screenshot.ps1`.

El contrato del canal lateral seguro debe pasar con:

```powershell
node .\tools\debug\verify-side-channel-contract.mjs --settle-ms 9000
```

Esta prueba debe producir `side-channel-shot.png` y `side-channel-profile.bin` en
`out\run\030_ehb_palette_zones` sin romper la conexion GDB del runner.

La convivencia de depuracion normal GDB con canal lateral debe pasar con:

```powershell
.\tools\debug\verify-gdb-step-side-channel.ps1 -Steps 3
```

Esta prueba pone un breakpoint GDB en `amg_debug_ready_probe`, continua, se para
en `T05swbreak`, avanza paso a paso por instrucciones y mantiene lecturas
laterales `state`/`regs` durante la ejecucion y en cada parada.
