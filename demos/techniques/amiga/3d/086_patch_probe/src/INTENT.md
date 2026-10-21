# INTENT — 086_patch_probe

## Qué hace

La aplicación compone una escena planar OCS de cuatro bitplanes y conserva `MoveSlot` para los opcodes que escriben los punteros `BPLxPT` y el registro `BPLCON1`. Durante la inicialización compara cada slot con la palabra de opcode ya materializada; durante cada `update` cambia los datos de los punteros por buffer y publica el coste en el run-status. `PATCH_PROBE_MODE` selecciona la ruta aislada o las variantes que combinan parcheo y relleno por plano.

## Qué pretende demostrar

La prueba mínima detecta si una copperlist materializada cambia el orden de sus palabras después de registrar los slots. El contador `m_bad_slots` debe permanecer en cero y el número de slots revisados debe coincidir con los registros de puntero y desplazamiento parcheados. El modo 8 replica conjuntamente el parcheo de ventana, el relleno tramado de la sala y los paneles sólidos para comprobar que la ruta combinada conserva el display.

## Elementos en pantalla

El modo predeterminado no dibuja geometría ni HUD: el plano sin inicializar deliberadamente observable es un campo negro, mientras que los datos parcheados son internos y se leen por `g_eng_run_status`. El modo 8 presenta un rectángulo tramado de cuatro bitplanes sobre fondo negro y paneles rectangulares sólidos; sus colores son oscuro, rojo, verde y blanco según la paleta OCS. El texto de estado y el contador de slots viven en el canal lateral, no en la imagen.

## Evolución temporal

En el modo predeterminado los punteros avanzan en pasos de dos bytes y `BPLCON1` cambia su fine scroll en cada frame, de modo que el parcheo recorre una ventana pequeña dentro del bitmap aunque este no tenga contenido dibujado. El modo 8 actualiza esa misma ventana junto con el patrón de sala y los paneles. Los modos de barrido de fase desplazan el momento del parcheo dentro del field para aislar sensibilidad temporal; los modos 5–9 separan el relleno tramado y sólido del parcheo de copperlist.
