# HOST-397 — plan de playback de síntesis

Comprueba que una voz sintetizada se convierte en una ventana PCM8 con metadatos de periodo, volumen y canal para Paula directa, o con amplitud limitada al rango del mixer de cuatro voces para `AUD0`.

El test no escribe registros ni instala una IRQ. La preparación queda fuera de la interrupción para que el backend solo tenga que publicar el buffer listo y cambiar el estado de Paula.
