# VALIDATION — 090_polyhedra_orbit

Estado: **ABIERTA / NO VERIFICADA por cadencia** (15,3–16,2 fps medidos frente al objetivo de 20 fps y al suelo de 25 fps de `AGENTS.md` §3). El usuario acepta quedarse en ~16 fps con la escena y los colores actuales. La geometría, el relleno y la ausencia de grietas/artefactos están verificados en el alcance que se detalla abajo; flicker/tearing y la comparación pixel a pixel de la versión de 4 planos quedan **SIN VERIFICAR**.

## 1. Malla y geometría

La tabla del poliedro se genera a partir del icosaedro (puntos medios de sus aristas, radio 90, winding exterior) y se valida en `init` (`mesh_is_closed()`): 30 vértices, 60 aristas, 32 caras, Euler=2, cada arista en exactamente dos caras, planitud ±12000, radio 89..92 y normales exteriores. Si falla, la demo no pinta y marca `0x00009010`. La tabla de adyacencia se construye con `build_edges` y exige 60 aristas en el poliedro y 12 en el cubo (`0x00009014`); una arista en más de dos caras se rechaza.

`orbit_separation_ok()` recorre 512 fases de la órbita y comprueba que las siluetas (cota conservadora con 2 px de margen) no se tocan y que todo cabe en el viewport (`0x00009011`).

Evidencia de cobertura (build de 5 planos, misma geometría, antes de la reescritura del relleno): la imagen de las fases 0 y 512 comparada contra una referencia 2D independiente (misma tabla, proyección, culling y painter) difería en 801/66.560 px (1,20 %) y 828/66.560 (1,24 %), todas en bordes de 1–2 px. Esa comparación **no cubre** la paleta ni el damero actuales.

En el build actual (4 planos, damero, culling 2D) el detector de grietas (`holes.mjs`) da **0 píxeles de fondo con objeto a los cuatro lados** sobre 4 frames de la secuencia; `backgroundInside` en el casco convexo es 2–30 px (cuantización del borde, como en el build anterior). La comprobación de visibilidad de caras con el signo del área 2D se validó visualmente contra el build de normal 3D (`K090_CULL2D=0/1`): mismas caras y mismos colores; no hay golden automático de esa firma.

## 2. Relleno, paridad y damero

- El contorno por plano dibuja solo la frontera de la unión de caras con ese bit; las aristas compartidas por dos caras del mismo plano se cancelan por paridad y el area fill `XOR` reconstruye el índice. Las aristas horizontales se descartan (contrato de `eng::retro::flat_shade_xor`).
- El plano 3 (entramado) tiene seleccionados los triángulos; tras el fill se le aplica un damero 2×2 con `D = A & B` sobre las filas del objeto, empezando en fila par. Los planos 0–2 no se tocan: el borde de la silueta alterna entre `base` y `base+8` y no puede quedar en color de fondo.
- `line_d_base` apunta a una palabra de descarte en Chip: el primer píxel de cada línea (que el modo línea escribe por `D`) cae ahí y no en la fila 0 visible. Antes de este ajuste aparecía un punto suelto en la esquina superior izquierda.
- El clear y el area fill usan una caja por sólido (poliedro y cubo) y el clear incluye las dos posiciones anteriores de cada uno. Las siluetas están garantizadas disjuntas, así que el clear de un sólido no borra al otro.

## 3. Cadencia (A500 debug, PAL, 141.876 ciclos/campo)

| Versión | Update (ciclos) | Total/frame | Campos | fps HUD |
|---|---|---|---|---|
| Contorno por plano, 5 planos, HUD cada frame | ~1,55 M | ~1,59 M | ~10,9 | 3,9 |
| Silueta XOR, 4 planos, damero, HUD/32 | 368–428 k | 435–466 k | 3,07–3,28 | **15,3–16,2** |
| A/B 2 colores sólidos, sin damero | 368 k | 435 k | 3,07 | 16,3 |

Desglose del perfil (build con damero, `profile.mjs`): `Project` 132 k (transform 66 k, culling+ensamblado 61 k), `Fill` 278 k (clear 28 k, contorno ≈143 k, area fill ≈30 k, damero 16 k, espera 11 k), `HUD` 6,8 k de media. El contorno son ~80 operaciones de línea (≈1,8 k cada una, con la preparación de Bresenham una vez por arista).

Conclusiones:

- El A/B de 2 colores sin damero (16,3 fps) demuestra que el damero y la variedad de color **no** son el cuello: quitarlos apenas cambia la cadencia.
- 20 fps exigirían ~355 k totales; el bucle añade ~67 k de espera de VBlank, así que el update tendría que bajar a ~288 k. Para ello habría que quitar el cubo o reducir la escena a un sólido, no solo cambiar colores.
- El suelo de 25 fps (2 campos) queda fuera de alcance con dos sólidos, 7 tonos y esta técnica en un 68000.

## 4. Visión (Ollama, prompt canónico)

`src/VISION.md` (generado con `vision-final.mjs`, 4 frames en orden temporal) describe un poliedro de caras triangulares de colores vivos (rojo, naranja, amarillo, cian, verde, magenta) y un cubo de tres caras visibles, ambos girando; el fondo es azul marino uniforme; **no observa halos, restos de posiciones anteriores, píxeles cortados ni elementos superpuestos**. El modelo confunde la lectura del HUD (`FRAME`/`FPS`) en algunas imágenes; es una limitación del OCR del modelo, no del render (las capturas largas muestran `FRAME 64/128` y `FPS 15.3–16.2`).

## 5. Límites y deuda

- **20 fps / 25 fps**: no alcanzados (ver §3). El usuario acepta la cadencia actual; si se quisiera 20, la vía es reducir la escena (un sólido o sin cubo) y volver a medir.
- **Flicker y tearing**: no verificados con la batería de flicker ni con capturas por campo. El damero de 1 píxel es sensible al muestreo de la captura; hay que comprobarlo antes de declarar la demo cerrada.
- **Comparación pixel a pixel del build de 4 planos**: no hay golden actualizado; la evidencia es la referencia geométrica del build anterior, el detector de grietas (0 en 4 frames) y la visión.
- **HUD**: se refresca cada 32 frames (≈0,5 s a 16 fps); en capturas cortas puede verse `FRAME 0 FPS 0.0` porque aún no ha llegado el primer refresco. No es un defecto de render.
- **Rutas CPU (1/2/3)**: se conservan como A/B de diagnóstico y no aplican el damero; no se han revalidado tras el cambio a 4 planos.

## 6. Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/090_polyhedra_orbit --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/090_polyhedra_orbit --settle-ms 7000 --sequence-frames 4
node ./tools/debug/profile.mjs 090_polyhedra_orbit A500_debug 4 --names Frame,Project,Fill,Hud,Clear,Lines,Pattern,Wait,Xform,Assemble
node ./tools/analyze/vision-final.mjs 090_polyhedra_orbit out/run/090_polyhedra_orbit/A500_debug/sequence --demo-dir demos/techniques/amiga/3d/090_polyhedra_orbit
# Pose fija (comparaciones deterministas):
DEMO_FLAGS="-DK090_FREEZE=1 -DK090_START_PHASE=512" bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/090_polyhedra_orbit --debug
```
