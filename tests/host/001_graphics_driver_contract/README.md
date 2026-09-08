# Test HOST-001: contrato de driver gráfico (GraphicsDriver / DisplayDriver)

Test unitario **host** (compila con g++ del sistema, sin WinUAE) que valida en
compilación los conceptos del ciclo de instalación del display definidos en
[`engine/include/eng/graphics/driver.hpp`](../../../../engine/include/eng/graphics/driver.hpp):

| Concepto | Qué exige | Qué tipos lo cumplen |
|---|---|---|
| `DisplayDriver<Driver, Backend>` | `takeover(backend)` + `install(backend)` | drivers y compositores que producen copperlist: `StaticEhbScene`, `TileScrollScene` (single/dual), `XlimitedDisplayComposer`, `XlimitedDualComposer`, `DpfDisplayComposer`, `XlimitedScene`. |
| `GraphicsDriver<Driver, Backend>` | todo lo de `DisplayDriver` + `Driver::id` + `begin_frame/end_frame` | solo drivers gráficos completos: `StaticEhbScene`. |

El test no instancia ni ejecuta nada: los `static_assert` fuerzan la resolución
de las expresiones `requires`, de modo que si un driver pierde un método o un
compositor deja de exponer el ciclo de display, el error aparece en compilación
(host) en lugar de en runtime. Usa un `MockBackend` que solo declara
`takeover_display`/`install_copper_list`, porque el contrato no depende del
backend concreto (Amiga, Mega Drive, PC…).

## Ejecución

Desde la raíz del repo:

```bash
bash tools/run-host-tests.sh tests/host/001_graphics_driver_contract   # solo este
bash tools/run-host-tests.sh                                            # todos
```

Salida: `OK: contratos GraphicsDriver/DisplayDriver validados en compilacion.`
y código de salida 0 (éxito).

## Relación con el bug de arranque/display

Este contrato formaliza el patrón que corrigió el bug de "doble texto + banda
cian" (`docs/debugging/DEBUG_DEMO_ARRANQUE_DOBLE_TEXTO_BANDA.md`): la separación
obligatoria entre `takeover` (toma de control del display, una sola vez, alineada
al VBlank) y `install` (swap de puntero COP1LC por frame, sin COPJMP1). Ver
también `MinimalBackend::takeover_display` y la política close-to-metal en
`docs/engine/architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md`.
