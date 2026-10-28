# HOST-496: save-under de celdas vivas (`MapCellSaveCache`/`MapCellSaveCacheSet`)

## Qué cubre

Valida en host el cache de save-under por celda del surface (tiles animados de la demo 207), sin WinUAE:

- `configure` reserva **un único bloque Chip** de `Capacity × cell_bytes`; no hay una reserva por celda (el `BlockPool` tiene 64 huecos y fragmentaba con 96 celdas).
- `capture` emite **un** `CopyRect` para todos los planos del destino (`bitplane_count = planes`) con el direccionamiento del anillo: plano 0, paso entre planos `row_bytes`, módulo de fila `row_bytes × planes - palabras × 2`.
- El buffer de guardado es planar compacto (`[plano][fila][palabra]`), con paso entre planos `words × 2 × height`.
- La captura se conserva: un segundo `capture` no emite trabajo y sólo marca el overlay como pintado.
- `restore` emite un `RestoreRect` inverso, deja `painted=false` y mantiene la captura para el siguiente frame.
- `release` libera la entrada sin restaurar (celda fuera de la ventana).
- Una celda mayor que el stride configurado se rechaza sin tocar el plan; sin slot libre, la captura falla y aumenta `failures()`.
- `MapCellSaveCacheSet` reserva dos arenas (tiles y sprites fusionados) y enruta cada caja a la clase que puede contenerla; una captura de 32×32 no consume el stride de las celdas de 16×16.

## Contrato que fija

| Elemento | Valor esperado |
|---|---|
| Arena de guardado | `Capacity × cell_bytes` en Chip |
| Celda de la prueba | 16×16, 6 planos, 192 bytes |
| Jobs por captura | 1 (`CopyRect`) |
| Jobs por restauración | 1 (`RestoreRect`) |
| Paso entre planos del anillo | `row_bytes` (44) |
| Paso entre planos del buffer | `words × 2 × height` (32) |
| Módulo de fila del anillo | `mod16(44 × 6 - 2)` |
| Módulo de fila del buffer | `0` |

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/graphics/496_cell_save_cache
```

Salida esperada: `HOST-496: save-under de celdas OK`.

## Alcance

Comprueba la geometría y el ciclo de vida del cache (jobs, punteros, módulos, captura única, restore, release y capacidad). No ejecuta el Blitter ni valida píxeles finales; la demo 207 y su secuencia cubren el render real.
