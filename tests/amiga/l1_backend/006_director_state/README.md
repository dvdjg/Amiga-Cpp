# L1-006: estado persistente de `SpriteDirector`

Sonda canónica para distinguir **“el director está vacío”** de **“el contador se ha reseteado”**. Da de alta un visual y ocho entidades simples, sin emitir ni dibujar, y publica en `g_eng_run_status.detail`:

| Bits | Valor |
|---|---|
| 0-7 | `1` si todas las comprobaciones pasan |
| 8-15 | entidades vivas; debe ser `8` |
| 16-31 | `0xBEEF` (magic de la sonda) |

El director está flanqueado por canarios (`0x13572468` antes, `0x24681357` después); si un array interno desborda, el test falla antes de que el contador pueda parecer válido.

## Ejecutar

```bash
bash tests/amiga/l1_backend/006_director_state/verify.sh
```

Salida esperada:

```text
[L1-006] OK: 8 entidades tras init, canarios intactos (detail=0xbeef0801).
```

## Depuración con GDB y bases de sección

**No calcular la dirección de `g_guarded` como `baseText + offset_ELF`.** En Amiga `.text`, `.data` y `.bss` se relocalizan por separado; una dirección calculada así puede leer ceros de otra zona y simular un reset inexistente.

Para localizar el objeto:

1. Consultar `state.sections` en el canal lateral. Para este ELF el orden es `.text`, `code`, `P61Code`, `.rodata`, `.init_array`, `.data`, `.bss`.
2. Obtener el offset de `g_guarded` respecto a `.data`:

   ```bash
   m68k-amiga-elf-nm -C out/demos/006_director_state/A500_debug/006_director_state.A500_debug.elf | grep g_guarded
   m68k-amiga-elf-readelf -S out/demos/006_director_state/A500_debug/006_director_state.A500_debug.elf
   ```

3. `dir_runtime = baseData + (nm_g_guarded - sh_addr(.data))`.
4. Leer los canarios en `dir_runtime` y `dir_runtime + sizeof(GuardedDirector) - 4`: deben ser los valores conocidos. Si no lo son, la base es incorrecta.
5. Buscar el magic `DE AD BE EF` en la ventana de datos como comprobación independiente; debe caer al final del objeto.

Un `count == 0` solo es diagnóstico si se ha leído en `dir_runtime + offset(director.count)` **después** de `mark_ready` y con los canarios intactos. Si la sonda está en `init` o antes, el cero es el estado inicial normal.

Referencia de la regla: `AGENTS.md` §1.17.1.
