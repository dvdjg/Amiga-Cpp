# 222 — Efectos de sprites HW apilados (bending + palette splitting)

Demo de que los efectos del sistema de sprites HW son **acumulables y aplicables a cualquier sprite**: las dos columnas de 16×64 de la demo 220 (canales 2 y 3) se **doblan** por línea (`drivers::emit_sprite_bend_into`) y, a mitad del sprite, el Copper **conmuta su paleta** (`COLOR20-23` del par 2/3): la mitad inferior cambia a magenta sin tocar el arte. Los dos efectos se emiten **intercalados por línea** en la misma copperlist, sobre los mismos canales y con el mismo contrato de DATA (filas + terminador `0,0`).

- **Bending**: `SPRxPOS/CTL` por línea con `VSTART = línea` (rearme del DMA; `../WinUAE-DBG/custom.cpp:4078-4090`).
- **Palette split**: `MOVE` a `COLOR20-23` entre las filas 30 y 31 (mecanismo real de color por zona; `sprite-layer.md` §3-§4).
- **Aplicable a cualquier sprite**: aquí sobre el par 2/3 (canales 2/3); la demo 221 lo aplica al par 0/1 (canal 0) y HOST-391 a cualquier canal asignado por el allocator.

## Medidas (A500_debug)

- `measure-fps.mjs`: **49.92 fps**, 1.00159 campos/frame.

## Validación

Visión local (Ollama, 2 capturas): «2 columnas onduladas (V invertida/lambda)… **cambio de color: cian/blanco arriba, magenta abajo**… **sin cortes, basura ni líneas sueltas**». Detalle en [`VALIDATION.md`](VALIDATION.md).

## Ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/222_sprite_effects_stack --debug
bash tools/run/run-demo.sh   demos/techniques/amiga/sprites/222_sprite_effects_stack --keep-running
node tools/debug/measure-fps.mjs 222_sprite_effects_stack A500_debug --json
```

## Referencias

- Demos 220 (bending) y 221 (palette split); HOST-436 y HOST-391.
- `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.3/§3.5 (composición de efectos).
