# input.device según ROM Kernel Reference Manual - Devices

Resumen de la documentación oficial del kernel para usar input.device correctamente.

## Arquitectura

- **Intuition**: SOLO para el menú principal (calculadora, launcher)
- **input.device**: SOLO durante demos/juegos
- **Flujo**: Menú (Intuition) → Demo (TakeSystem + input.device) → Salir demo → REMHANDLER + CloseDevice → Volver a Intuition

## Handler (IND_ADDHANDLER)

Según RKM y AmigaOS wiki:

1. **Estructura Interrupt**:
   - `is_Code`: dirección del handler
   - `is_Data`: dato pasado a A1 (opcional)
   - `is_Node.ln_Pri`: prioridad (Intuition=50; usar >50 para capturar antes)
   - `is_Node.ln_Name`: nombre del handler

2. **Convención de llamada** (68000):
   - A0 = puntero a cadena de InputEvent
   - A1 = is_Data
   - D0 = valor de retorno (NULL = consumir todo, no propagar)

3. **Reglas del handler**:
   - Si procesa todo y quiere consumir: retornar NULL (0)
   - Si quiere propagar eventos a handlers de menor prioridad: retornar puntero a cadena modificada
   - El handler recibe la cadena completa; puede desenlazar eventos o añadir nuevos

4. **IND_REMHANDLER**: quitar handler ANTES de CloseDevice

## Eventos

- **IECLASS_RAWMOUSE**: ie_X, ie_Y = deltas; ie_Code = IECODE_LBUTTON/RBUTTON (o IECODE_UP_PREFIX para release)
- **IECLASS_RAWKEY**: ie_Code = raw keycode; bit 7 (IECODE_UP_PREFIX) = release

## Orden correcto

```
[Entrar demo]
1. intuition_calc_close()  // cerrar ventana y pantalla Intuition
2. TakeSystem()            // LoadView(0), tomar display
3. engine_input_devices_open()  // OpenDevice + IND_ADDHANDLER
4. effect_create(), effect_loop()

[Salir demo]
1. effect_destroy()
2. engine_input_devices_close()  // IND_REMHANDLER + CloseDevice
3. FreeSystem()                  // restaurar display
4. intuition_calc_open()         // reabrir Intuition
```

## Referencias

- AmigaOS wiki: Input Device
- RKM Devices: Input Device, IND_ADDHANDLER, IND_REMHANDLER
- devices/input.h: IND_ADDHANDLER, IND_REMHANDLER
- devices/inputevent.h: InputEvent, IECLASS_*, IECODE_*
