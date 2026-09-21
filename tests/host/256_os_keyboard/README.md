# HOST-256: teclado del mini-SO (`eng/os/input.hpp`)

Test host del `KeyProducer`: corrige el **bit-reverse** del scancode de la CIA, distingue down/up
(bit 7) y mantiene los **modificadores** (Shift/Ctrl/Alt/Amiga).

## Qué comprueba

1. `reverse_bits7`: inversión de los 7 bits (p. ej. `0x60` ↔ `0x03`).
2. Un scancode produce `KeyDown`; con el bit 7, `KeyUp`; el `code` queda corregido.
3. Shift/Ctrl bajan y suben actualizan `qual`; una tecla normal con Shift pulsado lleva el
   modificador en el payload.

## Salida de referencia

```
OK: teclado (bit-reverse, down/up, modificadores) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/256_os_keyboard
```
