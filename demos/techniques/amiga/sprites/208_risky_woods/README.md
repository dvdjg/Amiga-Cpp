# 208_risky_woods — fondo por sprites (técnica *Risky Woods*, 3 franjas)

Demo del **reparto de canales de sprite por ventanas de reprogramación**
(`docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`), construida **por etapas**
(`docs/debugging/investigaciones/risky-woods-208-sprite-scroll.md`). El efecto final es un fondo de
**3 franjas** que cubren 320 px con scroll de coste ~0 CPU, reutilizando los 8 canales de sprite.

## Etapas

| Etapa | Qué | Estado |
|---|---|---|
| **E0** | 1 canal, 16 px, estático — estructura DMA + armado. | **OK** |
| **E1** | 8 sprites sueltos lado a lado = patrón de 128 px (una franja, figura coherente). | **OK** |
| **E2** | repetición a 320 px (Copper: WAIT + ráfaga por período). | **OK** |
| **E3** | scroll por punteros pre-shifteados (copperlist **estática**, ~0 CPU). | **OK** |
| **E4** | 3 franjas (8 sueltos / 6 + 2 objetos / 4 pares *attached*) con scroll común. | **OK** |
| **E5** | gate visual (Ollama/gemma3) + revisión de flicker. | **OK** (ver nota) |

## Qué hace (E4)

Tres bandas, cada una repartiendo los 8 canales de sprite de forma distinta:

- **Franja A** — 8 sprites sueltos (3 colores), patrón de 128 px; forma una **colina**.
- **Franja B** — 6 sprites sueltos (patrón de 96 px, un **valle**) **+ 2 objetos** en los canales
  0/1 que ondulan **por delante** del fondo.
- **Franja C** — 4 **pares *attached*** (15 colores) formando **anillos concéntricos** rainbow a
  todo el ancho.

El **scroll** es común: por frame se reescriben las palabras `SPRxPT` (punteros a las estructuras
pre-shifteadas) y se rotan columnas; la copperlist **no se regenera**.

## Contrato y hallazgos (lo que enseña la demo)

- **Prioridad = número de canal más bajo** → los objetos van en 0/1 (delante) y el fondo de B en
  2..7 (detrás). `docs/reference/emulators/winuae/sprite-color-priority.md`.
- **Head-start de la reposición**: el `WAIT` debe caer con antelación suficiente antes de la nueva
  X (si no, el primer canal de cada grupo parpadea y el par *attached* pierde los bits altos).
- **Presupuesto de ancho de banda del Copper**: cada `MOVE`/`WAIT` = 8 px lo-res. En los tramos
  *attached* (4 pares = 8 `MOVE` = 64 px = período) **hay que quitar los `WAIT` intermedios**, o el
  Copper se retrasa +8 px por período y el impar llega tarde (cae a 4 colores). Los tramos
  no-*attached* **sí** necesitan el `WAIT` por período.
- **Guarda entre bandas**: una línea neutra (`top+1`) entre bandas evita la franja sólida de la
  primera columna (el DMA recarga el `SPRxPT`/cabecera sin mezclar estructuras).

## Nota sobre el flicker (E5)

El `flicker-check` determinista marca candidatos (fondo en movimiento continuo + rotación de
columna del scroll), pero **la revisión visual (Ollama/gemma3 + inspección manual de frames) no
muestra artefactos**: se acepta la demo. El gate formal de flicker queda como deuda
(`measure-fps` reporta 0 porque el demo no actualiza el contador de frames del run-status).

## Evidencia

- `run-demo.sh 208_risky_woods` → READY; captura: colina (A), valle + 2 objetos delante (B),
  anillos rainbow a todo el ancho (C). Validado por visión local (gemma3) y por conteo de colores
  por columna (C = 16 colores/columna en los 320 px).

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running
```
