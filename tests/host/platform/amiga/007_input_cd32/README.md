# HOST-007 · input_cd32 (decodificación de botones CD32)

Test host de la parte pura del protocolo CD32: `eng::amiga::decode_cd32_buttons`
(flujo serie de 9 bits → máscara de botones, con degradación a joystick de 1/2
botones si falta la firma). Mismo mapeo que Sevgi.

## Qué valida

- Firma CD32 válida (bit8=1, bit7=0) conserva los 7 botones.
- Sin firma, degrada a joystick normal (solo azul/rojo, descarta el resto).
- Cada botón individual (azul, rojo, amarillo, verde, adelante, atrás, play).

## Estado

Pasa con `g++` nativo. La lectura serie real (`read_cd32_buttons`) es sensible al
timing y se valida en emulador, no en host.
