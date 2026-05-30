# 051_blitter_shifted_bobs

Demo especifica para validar shifts de Blitter.

`050_blitter_bobs` mantiene una base estable de save/restore con X alineada a 16
pixels. Esta demo prueba el siguiente escalon: dibujar un BOB cookie-cut en una X
no alineada usando los campos de shift de `BLTCON0` y `BLTCON1`, sin que la logica
de la demo escriba registros custom directamente.

La fuente del BOB incluye una word extra por fila. El Blitter puede desplazar la
mascara y los datos de color desde A/B hacia la siguiente word de destino, de modo
que un sprite de 32 pixels puede ocupar tres words de pantalla cuando empieza en
una X arbitraria.

Comandos:

```powershell
.\tools\build\build-demo.ps1 demos\051_blitter_shifted_bobs -DebugBuild
.\tools\run\run-demo.ps1 demos\051_blitter_shifted_bobs
.\tools\analyze\analyze-demo.ps1 demos\051_blitter_shifted_bobs
```
