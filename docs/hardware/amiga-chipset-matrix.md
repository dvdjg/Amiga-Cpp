# Matriz rápida: OCS / ECS / AGA / CD32

Referencia compacta para elegir técnica y máquina objetivo. El **AHRM 3rd ed.** en el repo cubre sobre todo **OCS/ECS**; para **AGA** fino (BPLCON3/4, FMODE, nuevos modos fetch, colores 24-bit, blitter “enhanced”) conviene cruzar con documentación AGA adicional cuando la incorpores al workspace (p. ej. *Amiga Hardware Reference Manual* secciones AGA, materiales Commodore, o guías legales tipo HRM/RKRM).

| Capacidad | OCS (A500 512K+512K típ.) | ECS (Fat Agnus, 1–2 MB chip) | AGA (A1200/A4000/CD32) | CD32 notas |
|-----------|---------------------------|------------------------------|-------------------------|------------|
| Chip RAM máx. típica | 512 Ki chip + slow | Hasta ~2 MB chip | 2 MB chip | Similar AGA, sin teclado |
| Resolución lores/hires | Sí | Sí | Sí + más modos | Sí |
| Colores playfield lores | Hasta 6 planos estándar (HAM/EHB según modo) | Similar + más chip | Hasta 8 planos, paleta extendida | AGA |
| Dual playfield | Sí (3+3 lores típ.) | Sí | Sí, más flexible | Sí |
| HAM / EHB | Sí (HAM 6 planes) | Sí | HAM8 etc. | HAM8 |
| Sprites | 8 canales, 16 px | Igual | Igual + más control color | Gamepad |
| Blitter | Estándar | Estándar | “Enhanced” (anchos mayores, límites distintos) | AGA blitter |
| CPU típico | 68000 @ 7.16 MHz PAL | 68000 | 68020/030 | 68020 |
| Audio Paula | 4 canales | 4 | 4 | 4 |

## Registros a revisar primero (todas las familias)

`BPLCON0`, `BPLCON1`, `BPLCON2`, `BPLCON3` (AGA), `FMODE` (AGA), `DMACON`, `INTENA`/`INTREQ`, punteros `BPLxPT*`, `BPLxMOD`, `COP1LC`/`COP2LC`.

## Referencias en este repo

- Índice al manual largo: [amiga-hardware-manual-index.md](amiga-hardware-manual-index.md)
- Fichas de técnicas y enlaces externos: [techniques/README.md](techniques/README.md)
- Perfiles WinUAE: [config/winuae/README.md](../config/winuae/README.md)

## Fuentes AGA explícitas (pendiente de añadir)

Cuando tengas PDF/Markdown con licencia clara, enlázalo aquí y en el índice del AHRM como “complemento AGA”; hasta entonces tratar AGA con el AHRM Apéndice C (ECS) más documentación dispersa verificada en hardware real o WinUAE.
