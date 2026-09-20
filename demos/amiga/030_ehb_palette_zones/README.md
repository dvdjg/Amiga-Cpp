# 030_ehb_palette_zones

Primera demo del futuro driver `EhbScene`.

La demo activa 6 bitplanes lowres en OCS PAL. Los indices 0..31 usan los registros
`COLOR00..COLOR31`; los indices 32..63 muestran esos mismos colores a media
intensidad mediante el modo Extra Half-Brite.

La imagen se genera como patron planar en Chip RAM:

- 320x256 pixels.
- 40 bytes por fila y bitplane.
- 6 bitplanes contiguos.
- Reticula de 8x8 celdas con indices 0..63.

El Copper cambia la paleta completa en tres zonas verticales:

- zona superior: paleta de referencia con primarios y secundarios;
- zona central: paleta calida;
- zona inferior: paleta fria.

Esta demo todavia no carga assets UAF-R. Su objetivo es validar los ladrillos
close-to-the-metal: bitplanes EHB, punteros BPLx, modulo, DMA y cambios de paleta
por raster.

La demo ya no programa esos registros directamente desde `main.cpp`. Usa
`scene::compose` (`engine/include/eng/graphics/scene/compose.hpp`) para reservar bitplanes y
copperlist en Chip RAM, activar 6 planos EHB y compilar zonas de paleta a Copper.
El codigo de la demo solo genera el patron planar de prueba y declara las paletas.

La reserva Chip se mantiene deliberadamente ajustada: seis bitplanes ocupan 61.440
bytes y la copperlist cabe en 1 KB. Pedir grandes bloques en AmigaDOS/Kick 1.3 puede
fallar por fragmentacion o memoria ya ocupada, asi que la demo no solicita margen
innecesario.

## Encendido / apagon

La demo arranca en negro y sube hasta `top_palette` en 32 frames mediante
`PaletteTransitionEffect` (`eng/graphics/effects/palette_transition.hpp`) en modo una sola
pasada (`ping_pong = false`). Despues repite un ciclo encendido/apagon de forma periodica
con el mismo efecto, re-enlazando origen/destino por fase (cada rampa es una sola pasada).
El efecto aporta un parche de paleta base y la escena (`palette_patchable`) parchea los
`COLORxx`; la demo no toca registros. READY se marca cuando el primer encendido ha
terminado (`kReadyFrame`), de modo que la captura del runner es siempre la escena
encendida, no un frame de la transicion.

## Verificacion

```powershell
.\tools\build\build-demo.ps1 demos\amiga\030_ehb_palette_zones -DebugBuild
.\tools\run\run-demo.ps1 demos\amiga\030_ehb_palette_zones
.\tools\analyze\analyze-demo.ps1 demos\amiga\030_ehb_palette_zones
```

El analizador especifico comprueba que la captura contiene colores de las tres
zonas y tambien muestras half-brite, por ejemplo rojo normal y rojo a media
intensidad en la zona superior.
