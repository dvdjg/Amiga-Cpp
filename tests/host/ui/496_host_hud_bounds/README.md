# HOST-496: HostHud con texto largo

Comprueba que `HostHud` no escribe fuera de su buffer cuando el nombre o la telemetría superan la capacidad: `TextLine` trunca de forma segura, se conservan canarios alrededor del objeto y el overlay recibe una cadena terminada en NUL dentro de `kTextCapacity`.
