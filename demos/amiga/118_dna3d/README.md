# Demo 118 · dna3d (port parcial)

Núcleo del efecto **`dna3d`** de `demoscene-repo-orig`: la doble hélice de ADN **generada en
runtime** (`GenCircularDoubleHelix`) y dibujada como **links** (líneas por Blitter) entre los
vértices de cada par de bases. Rotación del objeto `turns(frame*6)` y fase de la hélice
`frame*24`, igual que el original.

## Qué está portado

- **Generador de la hélice** (`gen_helix`): port 1:1 de `GenCircularDoubleHelix` con el
  vocabulario `Turns`/`sin`/`cos` (tabla 4.12 exacta) y los `swap16`/shifts del original.
  Validado por **HOST-178** contra la transcripción del original.
- **Transform+proyección** (`transform_all`): las macros `MULVERTEX` del original (los
  puntos generados son 12.4).
- **Links** (`draw_links`): port de `DrawLinks` (una línea por cara del asset `dna.c`,
  formato línea: dos índices contiguos).
- **Las dos hebras** (`draw_strands`): conecta nodos consecutivos de cada strand. Sin esto
  sólo se veían los pares de bases; con las hebras la **doble hélice** se reconoce (validado
  a ojo y con Ollama, que antes decía "no se ve un ADN").
- **Asset** `data/dna.c` → `Mesh3D` del engine (`Span<u8>` + grupos `Span<s16>`).

## Qué falta (ver `docs/demos/effects/DNA3D_PORT_PLAN.md`)

- **Flares**: el original dibuja un BOB OR en cada vértice (es el rasgo visual dominante).
- **Fondo `necrocoq`** en doble playfield con color por línea (Copper).
- **Doble buffer limpio**: el scaffolding reutiliza el anillo de 5 planos de la demo 079
  (rastro de frames), que no es del original; el display final será doble buffer.

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/amiga/118_dna3d --debug
bash tools/run/run-demo.sh demos/amiga/118_dna3d
```
