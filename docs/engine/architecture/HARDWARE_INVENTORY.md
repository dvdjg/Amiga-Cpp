# Inventario de hardware (`eng::hw`)

**Qué es.** La app **pregunta capacidades** de la máquina (familia Amiga completa) en vez de
suponerlas o de dispersar `#ifdef` de modelo. `eng::hw::probe()` sondea Exec, los custom chips, la
ROM y los mapas de memoria, y rellena un `HwInfo` **POD estable y consultable**; el juego solo lee.
Vive en `engine/include/eng/hw/info.hpp` (tipos, consultas y `probe` declarado); el sondeo lo
implementa el backend (`engine/src/platform/amiga_minimal/amiga_minimal_hw.cpp`).

```text
HwProbe::probe(HwInfo&)  →  HwInfo (POD estable)
       │
       ├─ CPU / FPU / MMU          (ExecBase->AttnFlags)
       ├─ Chipset + capacidades    (DENISEID; Akiko→C2P)
       ├─ Kickstart / Exec         (LibNode version/revision)
       ├─ RAM por tipo             (ExecBase->MemList)
       ├─ Display vigente          (lo declara la app: set_display)
       └─ Puertos de entrada       (asumidos; CD32 por POTGO)
```

## API pública

```cpp
#include <eng/hw/info.hpp>

eng::hw::HwInfo hw {};
eng::hw::probe(hw);                      // sondea; rellena lo que pueda

if (eng::hw::has_fast_ram(hw))      { cache.use_fast(hw.fast_ram_bytes / 2); }
if (eng::hw::is_aga(hw))            { scene.set_max_depth(eng::hw::max_planes(hw)); }
if (eng::hw::is_cd32(hw) && hw.caps.c2p_hw) { renderer.enable_akiko_c2p(true); }
if (eng::hw::cpu_at_least(hw, eng::hw::CpuKind::M68020)) { audio_cfg.sw_voices = 4; }
```

Tipos: `CpuKind`, `FpuKind`, `Chipset`, `MachineModel`, `MemRegionKind`, `PortDevice`,
`MemRegion`, `InputPortInfo`, `DisplayInfo`, `Caps`, `HwInfo`.

Consultas (baratas, sin hardware): `cpu_at_least`, `has_fast_ram`, `is_aga`, `is_ecs`, `is_cd32`,
`can_hires`, `max_planes`, `max_indexed_colors`, `cpu_name`/`chipset_name`/`model_name`.

Heurísticas puras (testeables sin hardware): `guess_model(caps, chipset, chip_ram_bytes)`,
`classify_region(is_chip, is_fast, base)`, `model_has_rtc(model)`.

Display: `set_display(HwInfo&, width, height, depth, hires, lace, ham, ehb)` (o desde un
`DisplayInfo`).

## Campos y su origen

| Campo | Origen |
|-------|--------|
| CPU / FPU / MMU | `ExecBase->AttnFlags` (bits `AFB_*`; 68060 por `AFB_68060`, FPU 040 por `AFB_FPU40`) |
| Kickstart / Exec | `ExecBase->LibNode.lib_Version` / `lib_Revision`; `SoftVer`; `VBlankFrequency` (NTSC/PAL) |
| Chipset OCS/ECS/AGA | `DENISEID` ($DFF07C): Lisa `0xF8` → AGA, ECS `0xFC`, resto OCS |
| Akiko / C2P | **pendiente**: el campo `caps.akiko`/`caps.c2p_hw` existe y `is_cd32`/`guess_model` lo usan, pero el sondeo no está implementado (sin detección verificada en el runner, solo A500) |
| Chip / Fast / Slow | `ExecBase->MemList` (`MemHeader`), clasificado por `MEMF_CHIP`/`MEMF_FAST` y dirección |
| Modelo | heurística `guess_model` (chipset + RAM de chip + Akiko) |
| Display | lo **publica la composición** (`Scene::bind_hw_info` → `hw::set_display`) al programar el modo; la app también puede declararlo con `set_display` |
| Puertos 1/2 | política del juego (ratón / joystick); el CD32 pad se sondea por POTGO |

## Sondeo: decisiones

- **CPU/FPU/MMU** salen de `AttnFlags`; la familia es ordenada (`M68020 >= M68010`) y `Other` no se
  compara. `AttnFlags` marca 020 en adelante de forma acumulativa, así que se comprueba de mayor a
  menor.
- **Chipset** por `DENISEID`, que funciona también sin Exec. `ecs_denise`/`ecs_agnus`/`alice`/`lisa`
  se derivan del chipset detectado. **Akiko (CD32)** queda pendiente: no se inventa un registro sin
  una detección verificada.
- **RAM** se recorre con `Forbid`/`Permit` sobre `MemList`. Cada `MemHeader` se clasifica por
  atributos y base: el **slow RAM** (ranger, `$C00000-$D80000`) es `MEMF_CHIP` pero Agnus no lo ve,
  así que se clasifica por **dirección** (gana a `MEMF_CHIP`); Zorro II (`$200000-$A00000`) es Fast.
- **Display**: `BPLCON0` es de **solo escritura** (leerlo devuelve basura), así que el modo vigente
  no se puede leer de forma fiable. Lo **publica la composición**: `Scene::bind_hw_info(hw)` liga el
  inventario y, al inicializar la escena, `publish_display()` llama a `hw::set_display` con el modo
  derivado de los recursos (`Scene::display_info()`: ancho, alto, planos y HAM/EHB del `SceneMode`).
  `probe()` solo deja una estimación validada al arranque (ventana DIWSTRT/DIWSTOP + `BPLCON0`,
  descartada si no cuadra con los planos máximos del chipset).
- **Entrada**: el hardware no enumera de forma fiable qué hay enchufado. `port1`/`port2` llevan la
  **política** del juego (ratón en el 1, joystick en el 2) con `detected=false`; el sondeo real del
  CD32 pad (protocolo POT, `input_cd32`) marcaría `detected=true`.
- **Modelo**: A1200 y A4000 son ambos AGA y solo se distinguen por la RAM de chip (aproximación);
  A600 y A500+ tampoco se distinguen sin la ROM de producto. Se documenta como heurística, no como
  identificación exacta.

## Cómo lo usa el juego

```cpp
eng::hw::HwInfo hw {};
eng::hw::probe(hw);

// La escena declara el display vigente al programar el modo:
scene.bind_hw_info(hw);
composition::compose(scene, mem, composition::planar(320, 256, 6),
                     composition::ocs_a500,
                     composition::display(composition::kPal320x256, kBplcon0_Ehb));

const eng::u8 depth = eng::hw::max_planes(hw);          // 6 OCS/ECS, 8 AGA
playfield.set_max_depth(depth);
asset_cache.set_fast_budget(hw.fast_ram_bytes / 2);
input.enable_cd32_port2(hw.port2.is_cd32_pad);

// `hw.display` refleja el modo de la escena (320x256x6, EHB, 64 colores).
```

## Verificación

- **Demo 205** (`demos/amiga/205_hw_probe`): ejecuta `probe()`, liga la escena con
  `bind_hw_info` (el display sale de la composición) y muestra el `HwInfo` en el overlay del
  depurador. Verificada en **A500 (OCS, 68000, Kickstart 34.2, 512 KB chip + 504 KB slow)**.
- **HOST-234**: comprueba que `Scene::bind_hw_info` publica el tamaño, la profundidad y los colores
  de la escena en el `HwInfo`.
- **HOST-235** (`tests/host/235_hw_info`): cubre consultas, display, nombres y heurísticas, incluidas
  las ramas **AGA** y **CD32** por lógica (el runner solo emula A500).

> Las ramas AGA y CD32 (incluido `caps.c2p_hw`) están validadas **por lógica** (HOST-235), no sobre
> hardware/emulador AGA; el runner no ofrece esa configuración.
