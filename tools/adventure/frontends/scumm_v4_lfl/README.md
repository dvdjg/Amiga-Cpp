# Frontend SCUMM V3/V4 `.LFL` (`scumm_v4_lfl/`)

Lee las copias que guardan salas en contenedores **`.LFL`** (Amiga V3/V4 y FM-Towns): índice (`00.LFL`), salas (`NNN.LFL`), bloques y guiones (descumm `-3`/`-4`). Complementa al frontend V5 (`../scumm_v5/`), que cubre `.000/.001`.

## Formato (oráculo `resource_v4.cpp`, `room.cpp`, `resource.cpp`)

```
índice 00.LFL:  [u32LE tamaño][u16LE tag]  ·  tag incluido en tamaño (6 B de cabecera)
                tags por par invertido: 0R salas · 0S guiones · 0N sonidos · 0C costumes · 0O objetos
                tabla: u16LE nº + entradas (roomno u8 + offset u32LE); objetos: solo offset u32LE
sala   NNN.LFL: [u32LE tamaño][tag ASCII 2 B]  ·  primera `RO` (envuelve la cabecera de sala)
                dentro de RO: HD · BX · PA · BM · OI… · NL · SL · OC… · EX · EN · LC (nº locales)
                tras  RO: SC (guiones globales) · SO (sonidos) · CO (costumes)
```

- La tabla `S0` del índice da `(sala, offset)` de cada guion global: el offset apunta al bloque `SC` correspondiente (mismo fichero), lo que permite nombrar la salida `SCRP_<id>`.
- **Cifrado**: `getEncByte` (`resource.cpp`) → V4 con clave: sala ≠ 0 y < 900 → XOR 0x69 (las de índice 0/900+ no). El frontend detecta cabecera inválida y reintenta con XOR.
- **Prefijos de bytecode V3** (para descumm): `SC` 1 byte; `LS` id u8 + offset u16LE relativo a la cabecera de sala (`room.cpp:794`); `EX`/`EN` tal cual.

## Alcance verificado

- **Indy3 FM-Towns (ES)**: índice `0R`=99 salas / `0S`=199 / `0N`=199 / `0C`=199 / `0O`=1000 (offset u32LE); **87 salas** con bloques y **490 guiones** descompilados con `descumm -3` (globales nombrados por id, locales y EX/EN).
- **V4 Amiga**: el parser del índice/salas/bloques (y el cifrado 0x69) es el mismo camino; la validación con una copia Amiga (p. ej. MI1 `Rooms/*.LFL`) queda **pendiente de materializar el ADF** (SIN VERIFICAR).

## Uso

```bash
node tools/adventure/frontends/scumm_v4_lfl/extract.mjs index <00.LFL> [--out f.json]
node tools/adventure/frontends/scumm_v4_lfl/extract.mjs rooms <00.LFL> [--out dir] [--room N]
node tools/adventure/frontends/scumm_v4_lfl/extract.mjs dump-game <00.LFL> --out <dir> [--version 3|4]
```

`DESCUMM` (entorno) permite fijar el binario de descumm; por defecto usa `../SCUMM/scummvm-tools-2.9.0/descumm(.exe)`.
