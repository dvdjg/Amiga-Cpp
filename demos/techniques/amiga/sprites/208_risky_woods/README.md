# 208_risky_woods — fondo por sprites *Risky Woods* (etapa E0)

Demo del **reparto de canales de sprite por ventanas de reprogramación**
(`docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`), construida **por etapas**
(`docs/debugging/investigaciones/risky-woods-208-sprite-scroll.md`). El objetivo final es un
fondo de 6 canales (patrón de 96 px) que cubra 320 px con scroll de coste ~0 y 2 canales
libres para objetos.

## Etapas

| Etapa | Qué | Estado |
|---|---|---|
| **E0** | **1 canal, 16 px, estático** — estructura DMA + armado. | **OK** |
| **E1** | **8 sprites sueltos lado a lado = patrón de 128 px** (una franja, figura coherente). | **OK** |
| **E2** | **repetición a 320 px** (Copper: WAIT + ráfaga por período). | **OK** |
| E3 | scroll por punteros pre-shifteados (copperlist **estática**, ~0 CPU). | — |
| E4 | 3 franjas con las variantes (6 sprites + objetos / 8 / attached). | — |
| E5 | gate visual (Ollama) + FPS/flicker. | — |

## E1 — qué hace

- **8 canales sueltos** (0..7, modo 4 colores), cada uno con su estructura DMA, forman una
  **figura coherente** de 128×80 (colina) que **tesela** (se repite sin costura).
- Se dibuja en el **borde izquierdo del display** (`kDisplayX0 = 128`; el borde usa `COLOR0`).
- Muestra los **3 colores** de cada sprite (valores 1/2/3): suelo, cresta y cielo. Los 4 pares
  se cargan con los mismos 3 colores para que la figura sea coherente a lo ancho.
- Los canales no usados apuntan a una estructura **desactivada** (`VSTART=VSTOP`), para que el
  DMA no los auto-arme (el bug de los huecos; §2.5 del diagnóstico).

## Rendimiento (principio de diseño)

- La copperlist **no se regenera por frame**. En E1 no hay trabajo de Copper por línea ni por
  frame; en E3 el scroll irá por **cambio de punteros `SPRxPT` pre-shifteados + bit H0**.

## Evidencia (E1)

- `run-demo.sh 208_risky_woods` → READY; captura: figura de 128 px (x=128..255) con cielo azul,
  suelo marrón y cresta amarilla.

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running
```
