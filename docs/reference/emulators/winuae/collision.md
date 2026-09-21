# WinUAE — colisión de sprites (`CLXCON`/`CLXDAT`)

Cómo implementa WinUAE la colisión de hardware de sprites, leído de su fuente
(`../WinUAE-DBG/`): `drawing.cpp` (cálculo por píxel) y `custom.cpp` (registros).

## 1. `collision_level` (preferencia de WinUAE)

La detección está **desactivada por defecto** según una preferencia:

| `currprefs.collision_level` | Qué detecta |
|---|---|
| 0 | **nada** (`CLXDAT` se queda a 0) |
| ≥ 1 | sprite ↔ sprite |
| > 1 | sprite ↔ bitplane |
| ≥ 3 | bitplane ↔ bitplane |

`drawing.cpp:4192` (`denise_render_sprites2`). Si el runner no la sube, `CLXDAT` es **siempre 0**.
En nuestro runner el nivel estaba **> 1** (la colisión sprite-bpl sí registra), así que este no
era el problema.

## 2. La trampa par/impar (el bug real)

`drawing.cpp:4196-4211`:

```c
if ((apixel & clxcon_bpl_enable_aa) == clxcon_bpl_match_aa) {   // enable_aa = enable & 0xAA
    clxdat |= sprbplcoltable[...] << 4;                          // bits 5-8: planos PARES vs sprite
    if ((apixel & clxcon_bpl_enable_55) == clxcon_bpl_match_55)  // enable_55 = enable & 0x55
        clxdat |= sprbplcoltable[...] << 0;                      // bits 1-4: planos IMPARES vs sprite
}
```

con `clxcon_bpl_enable = (clxcon >> 6) & 63` (ENBP6..ENBP1) y
`clxcon_bpl_match = (clxcon & 63) & enable` (`drawing.cpp:3255-3263`).

**Consecuencia**: si habilitas solo planos de un grupo (p. ej. `ENBP1`, impar → `enable_aa = 0`),
la comparación del grupo vacío `(apixel & 0) == 0` es **siempre cierta** → el bit de **planos
pares vs sprite** se enciende **siempre**, aunque no haya solape. Es el *«if all bitplanes are
excluded, then a bitplane collision will always be detected»* del AHRM, pero **por grupo
par/impar**, no global.

→ Para detectar solape con un plano **impar** (`BPL1`), mirar **`CLXDAT` bit 1**; con un plano
**par** (`BPL2`), **bit 5**. Encender ambos grupos (`ENBP=0x3F`) evita la ambigüedad.

## 3. Mapa de bits de `CLXDAT`

AHRM Table 7-3 + `sprcoltable` (`drawing.cpp:3400-3429`, sprite↔sprite) y `sprbplcoltable`
(sprite↔bitplane):

| Bit(s) | Significado |
|---|---|
| 1-4 | planos **impares** vs sprite 0/1, 2/3, 4/5, 6/7 |
| 5-8 | planos **pares** vs sprite 0/1, 2/3, 4/5, 6/7 |
| 9-14 | sprite ↔ sprite (pares 0/1↔2/3, …) |
| 0 | planos pares ↔ planos impares (requiere `collision_level ≥ 3`) |

`CLXDAT` (`custom.cpp:4169`): devuelve `clxdat | 0x8000` (bit 15 siempre 1) y **lo pone a 0**
(se autolimpia). Es lectura de palabra en `$DFF00E`.

## 4. `ENSP` → sprites

`expand_colmask` (`drawing.cpp:3241-3253`): los sprites **pares** participan siempre
(`sprcolmask = 0x40|0x10|0x04|0x01` = canales 6,4,2,0); los bits `ENSP` (15-12) añaden los
**impares** (`0x80,0x20,0x08,0x02` = canales 7,5,3,1).

## 5. Implicación para el engine

- La utilidad `graphics/sprite_collision.hpp` es correcta. El error estaba en la **demo 206** al
  comprobar `odd || even` (el bit 5 es siempre 1 con `ENBP` impar). Corregido: **solo el bit del
  grupo habilitado**.
- **Validado** en `demos/amiga/206_sprite_collision`: sprite sobre el plano → bit 1 (colisión,
  `COLOR00` rojo); fuera → 0 (`COLOR00` navy).
