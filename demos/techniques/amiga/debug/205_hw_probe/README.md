# Demo 205 — inventario de hardware (`eng::hw::probe`)

Ejecuta `eng::hw::probe()` **una vez** y muestra el `HwInfo` resultante en el overlay del depurador:
modelo, chipset, CPU/FPU, Kickstart, RAM por tipo (chip/fast/slow/total y nº de regiones), display
vigente y puertos de entrada. Es la evidencia visual del sondeo y un ejemplo de la API pública.

El display **no se declara a mano**: la escena de composición (`composition::Scene`) programa el
modo (320x256x5) y lo publica en el `HwInfo` con `bind_hw_info` → `hw::set_display`; la demo solo
lee `hw.display`. El resto (CPU, chipset, Kickstart, RAM, modelo) sale del sondeo real.

Además registra un **efecto por la lista ordenada** de la escena (`Scene::add_effect`, ver
`docs/engine/architecture/EFFECT_MODEL.md`): un degradado de bandas que declara su tramo
(`Plan::reserve_band`, que detecta solapes) y su coste (`Plan::note_effect_cost`, que suma al
presupuesto). El overlay muestra la huella declarada (`decl`) y el coste acumulado frente a la
capacidad. El callable es un **functor miembro** (`EffectTask`): `add_effect` guarda un
`FunctionRef` no propietario y una lambda temporal quedaría colgando.

Documentación y fuentes del sondeo: `docs/engine/architecture/HARDWARE_INVENTORY.md`.
Test de la parte pura: `tests/host/platform/amiga/235_hw_info`.

## Compilar / ejecutar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/debug/205_hw_probe --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/debug/205_hw_probe --wait-ms 8000
```

El overlay necesita algo de tiempo en aparecer; con el `--wait-ms` por defecto puede capturarse la
pantalla de arranque. `--wait-ms 8000` da margen.

## Resultado de referencia (A500, Kickstart 1.3)

```
Model: A500   Chipset: OCS   CPU: 68000
Kickstart: 34.2   NTSC: no   FPU: no
Chip RAM: 512 KB
Fast RAM: 0 KB
Slow RAM: 504 KB
Total RAM: 1015 KB   regions: 2
Display: 320x256x5   colors: 32   (scene)
hires: no   lace: no   HAM: no   EHB: no
Caps: ecs=no aga=no akiko=no c2p_hw=no mmu=no
fx (lista): 1 efecto   bands: 24   decl: 24+96   coste: 96/2048 ok
Port1: mouse   Port2: joystick   (assumed, not detected)
```

> El slow RAM reporta 504 KB (no 512) porque `MemList` excluye la zona reservada por Kickstart.
> Las ramas AGA y CD32 no se pueden emular con el runner actual (solo A500); están cubiertas por
> lógica en HOST-235.
