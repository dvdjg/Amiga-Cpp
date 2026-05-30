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
`StaticEhbScene`, definido en
`engine/include/amg/graphics/drivers/ehb_scene.hpp`, para reservar bitplanes y
copperlist en Chip RAM, activar 6 planos EHB y compilar zonas de paleta a Copper.
El codigo de la demo solo genera el patron planar de prueba y declara las paletas.

La reserva Chip se mantiene deliberadamente ajustada: seis bitplanes ocupan 61.440
bytes y la copperlist cabe en 1 KB. Pedir bloques grandes en AmigaDOS/Kick 1.3 puede
fallar por fragmentacion o memoria ya ocupada, asi que la demo no solicita margen
innecesario.

## Verificacion

```powershell
.\tools\build\build-demo.ps1 demos\030_ehb_palette_zones -DebugBuild
.\tools\run\run-demo.ps1 demos\030_ehb_palette_zones
.\tools\analyze\analyze-demo.ps1 demos\030_ehb_palette_zones
```

El analizador especifico comprueba que la captura contiene colores de las tres
zonas y tambien muestras half-brite, por ejemplo rojo normal y rojo a media
intensidad en la zona superior.
