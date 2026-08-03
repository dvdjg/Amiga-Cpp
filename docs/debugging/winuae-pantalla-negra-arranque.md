# Pantalla negra al arrancar WinUAE (sistema no bootea)

## Causas habituales

1. **Falta la ROM de Kickstart** o la ruta en el .uae no es correcta.
2. **Configuración A500 incompleta**: con solo `cpu_type`, `chipset`, `chipmem_size` y `kickstart_rom_file` a veces falta algo (p. ej. quickstart o tipo de floppy).
3. **`use_gui=no` + `-G`**: en algunos builds de WinUAE (p. ej. winuae-gdb) arrancar sin ventana puede dejar la pantalla negra aunque la CPU arranque.
4. **Ruta del disco con espacios** sin entrecomillar: el path de `floppy0` puede cortarse.

## Qué hacer

### 1. Usar quickstart A500 en el .uae

En `C:\Amiga\A500-Dev.uae` (o el config que uses) asegura tener al menos:

```ini
; A500 con Kickstart 1.3 — arranque desde DF0
quickstart=a500,1
kickstart_rom_file=C:\Amiga\KICK13.rom
```

`quickstart=a500,1` fija A500 + KS 1.3 y el resto de opciones por defecto (chip, floppy, display). Si quieres chipmem 1 MB:

```ini
quickstart=a500,1
chipmem_size=1
kickstart_rom_file=C:\Amiga\KICK13.rom
```

### 2. Probar con ventana (sin use_gui=no)

Para descartar que sea el modo “sin GUI”:

- **MCP / script**: antes de lanzar WinUAE, define en el entorno:
  ```powershell
  $env:WINUAE_USE_GUI_NO = "0"
  ```
  Así el MCP no pasará `-s use_gui=no` y debería abrirse la ventana de WinUAE. Si con esto ves imagen, el problema era el arranque sin ventana.

### 3. Comprobar la ROM

- Que exista `C:\Amiga\KICK13.rom` (o la ruta que pongas en `kickstart_rom_file`).
- Que sea una ROM de Kickstart 1.3 válida para A500 (512 KB, checksum correcto).

### 4. Rutas del disco con espacios

El MCP ya entrecomilla la ruta de `floppy0` cuando tiene espacios. Si lanzas WinUAE a mano, usa comillas, por ejemplo:

```text
-s "floppy0=C:\Amiga\FullSet\J\Jim Power in Mutant Planet_Disk1.zip"
```

### 5. Orden de argumentos

El MCP hace: `-f <config>` y luego `-s floppy0=...`, así que la config se carga primero y el disco se aplica después. No hace falta cambiar el orden.

## Resumen

| Síntoma              | Revisar |
|----------------------|--------|
| Pantalla negra       | quickstart en .uae, `WINUAE_USE_GUI_NO=0`, ruta de Kickstart, ruta del disco entrecomillada |
| Ventana no se abre   | Probar sin `-G` / sin `use_gui=no` (WINUAE_USE_GUI_NO=0) |
| “ROM not found”      | Ruta absoluta a la ROM en el .uae y que el archivo exista |

Ver también: [winuae-y-adf.md](winuae-y-adf.md), [diagnóstico-adf-negro.md](diagnóstico-adf-negro.md).
