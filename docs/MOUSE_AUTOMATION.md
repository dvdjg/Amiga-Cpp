# Automatizacion del raton de WinUAE

Este documento explica como deben comportarse las pruebas automatizadas cuando
necesiten raton. La prioridad es clara: WinUAE no debe capturar ni restringir el
puntero de Windows, pero las pruebas si deben poder mover el raton emulado del
Amiga y pulsar botones.

## Configuracion usada por el runner

`tools/run/run-demo.mjs` genera una configuracion temporal a partir de
`config/mcp-amiga-c-debug.uae` y fuerza estas opciones:

```text
win32.start_not_captured=yes
win32.active_capture_automatically=no
win32.absolute_mouse=yes
absolute_mouse=none
```

`win32.absolute_mouse=yes` selecciona el camino Win32 que evita el warping y la
captura clasica del cursor. `absolute_mouse=none` deja desactivado el modo
Amiga-side mousehack/tablet, porque en `WinUAE-DBG/doc/MOUSE-ABSOLUTE-TODO.md`
esta documentado que `absolute_mouse=mousehack` puede capturar el raton al
arrancar o dejarlo atrapado en una linea.

## Movimiento programable

La herramienta reutilizable es:

```powershell
.\tools\input\mouse-path.ps1 -From 32,40 -To 280,170 -Control 160,10 -Click
```

Internamente se conecta al servidor GDB/monitor de WinUAE ya arrancado y envia:

```text
input mouse abs x y
input mouse button boton estado
```

Eso mueve el raton emulado del Amiga sin usar el raton fisico del sistema. Las
coordenadas se expresan en pantalla Amiga low-res y se limitan por defecto a
`0..319` en X y `0..255` en Y.

## Trayectorias soportadas

- Sin `-Control`: trayectoria lineal.
- Con `-Control`: curva Bezier cuadratica.
- Con `-Control` y `-Control2`: curva Bezier cubica.
- Con `-Click`: pulsa y suelta al final.
- Con `-Drag`: mantiene pulsado durante toda la trayectoria.
- Con `-Screenshot`: guarda una captura justo despues de inyectar la entrada.

Ejemplo de arranque, trayectoria y cierre manual:

```powershell
.\tools\run\run-demo.ps1 demos\000_toolchain_cpp23 `
  -WaitMs 3000 `
  -MouseFrom 24,24 `
  -MouseTo 300,220 `
  -MouseControl 150,4 `
  -MouseClick
```

El runner integrado es el camino recomendado para regresiones porque inyecta el
movimiento mientras la conexion GDB original sigue abierta. En algunas sesiones
WinUAE no vuelve a aceptar una conexion GDB nueva tras desconectar el cliente.

Tambien existe una herramienta standalone para sesiones donde el servidor GDB ya
este aceptando conexiones:

```powershell
.\tools\input\mouse-path.ps1 -From 24,24 -To 300,220 -Control 150,4 -Click
```

## Politica para nuevas pruebas

Las demos futuras que necesiten raton deben:

1. Lanzarse con el runner normal, que ya aplica las opciones anti-captura.
2. Usar `tools/input/mouse-path.ps1` o su modulo Node para inyectar movimiento.
3. Capturar pantalla o leer estado por GDB/profiler para validar el resultado.
4. No depender de que el puntero fisico de Windows este dentro de la ventana.

Si mas adelante necesitamos input absoluto de Workbench/Intuition mediante
mousehack, se debe aislar en una prueba especifica porque no forma parte del
camino seguro de automatizacion.
