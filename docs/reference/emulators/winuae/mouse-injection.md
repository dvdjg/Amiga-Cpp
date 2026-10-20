# Inyección de ratón emulado

Las herramientas de depuración deben inyectar el ratón como entrada del puerto Amiga, no como movimiento del puntero físico de Windows. Las demos que leen el ratón por `JOY0DAT` consumen diferencias de contador por frame; una trayectoria se representa como una secuencia de pequeños deltas relativos y los botones como eventos de puerto.

## Ruta de entrada

| Operación de depuración | Implementación WinUAE-DBG | Semántica para el invitado |
|---|---|---|
| `input mouse move dx dy` | `od-win32/barto_gdbserver.cpp:1827-1829`, `:3800-3808`; `inputdevice.cpp:5772-5775`, `:6660-6668` | `send_input_event` activa la asignación del puerto y entrega eventos `MOUSE1_HORIZ`/`MOUSE1_VERT`; el Amiga observa los deltas en `JOY0DAT`. |
| `input mouse abs x y` | `od-win32/barto_gdbserver.cpp:1832-1835`, `:3809-3816`; `inputdevice.cpp:9813-9837` | El handler activa el puerto con un evento relativo neutro y después procesa la posición absoluta del dispositivo host como delta respecto a `oldm_axis`; no fija directamente el cursor lógico de una aplicación. |
| `input mouse button n state` | `od-win32/barto_gdbserver.cpp:1838-1847`, `:3817-3827`; `inputdevice.cpp:9649-9674`; `od-win32/dinput.cpp:4516-4522` | `n=0/1/2` actualiza el botón izquierdo/derecho/central del dispositivo de ratón 0, después de activar su mapa de entrada. |
| Salida temprana de `setmousestate` | `inputdevice.cpp:9785-9797` | La vía relativa de depuración la evita al emitir eventos `MOUSE1_HORIZ`/`MOUSE1_VERT`; la vía absoluta aún depende de que el dispositivo WinUAE esté configurado. |

El modelo de entrada del Amiga coincide con el AHRM 3.ª edición, capítulo 8, “Reading Mouse/Trackball Controllers”: los pulsos del ratón incrementan/decrementan contadores X/Y y el software deduce la magnitud y dirección restando la lectura anterior (`docs/reference/ahrm/Amiga Hardware Reference Manual 3rd edition (Commodore Amiga Inc.) (Z-Library).cat.md:6793-6801`, `:6839-6858`). `inputdevice.cpp:9813-9837` conserva la base absoluta del dispositivo host para la siguiente conversión; esa base no tiene por qué coincidir con la posición inicial del puntero dentro de una demo.

## Contrato de automatización

Para juegos que leen `JOY0DAT`, la ruta recomendada es relativa: se observa o conoce la posición lógica del puntero, se calcula una curva por tramos y se envía `input mouse move` con los deltas entre muestras. La lectura del invitado debe convertir cada diferencia de contador en un delta signed de 8 bits, incluidos los wraps `0xFF→0x00` y `0x00→0xFF`; `eng::amiga::mouse_counter_delta` aplica ese contrato y HOST-006 cubre positivos, negativos y los límites ±128. El modo absoluto queda para consumidores mousehack/tablet o cuando se conoce la base del dispositivo WinUAE. El MCP expone ambas opciones en `winuae_input_mouse` (`mode=path`, `coordinate_mode=relative|absolute`); el modo relativo es el predeterminado y permite click o drag con liberación garantizada del botón.

La referencia Amiga de uso está en [`MOUSE_AUTOMATION.md`](../../../emulation/MOUSE_AUTOMATION.md). La demo `002_adv_verbs` sirve para verificar la ruta completa desde el MCP hasta la selección de verbo o hotspot; el guion de atracción verifica la lógica de juego, pero no reemplaza un click emulado observado en pantalla.
