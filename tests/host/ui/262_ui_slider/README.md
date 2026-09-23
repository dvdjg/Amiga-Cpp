# HOST-262: GUI — `Slider`

Test host del widget `Slider` (`engine/include/eng/ui/slider.hpp`): control deslizante sobre un
`s16*` externo (`min..max`), sin heap. Ver `GUI_LIBRARY.md` §11.

## Qué comprueba

1. **Click** mapea la posición `x` al valor (inicio→`min`, final→`max`, medio→mitad).
2. `MouseUp` dentro actualiza el valor a su posición.
3. **Flechas** con foco (`Left`/`Down` bajan, `Right`/`Up` suben, acotado a `[min,max]`).
4. **Dibujo**: pista hundida + pomo en la posición del valor (se lee el color por píxel).

## Salida de referencia

```
OK: GUI Slider (click/flechas/dibujo) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/262_ui_slider
```
