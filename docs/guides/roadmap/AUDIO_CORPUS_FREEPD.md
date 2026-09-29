# Corpus de pruebas FreePD

FreePD cerró su sitio original en 2025. La colección archivada `freepd` de Internet Archive conserva copias MP3 atribuidas a sus autores y marcadas como material Free Public Domain en los metadatos de la colección. Los ficheros no se incorporan al repositorio: se descargan a `out/assets/audio/freepd/` cuando el servicio de archivo está disponible.

## Fuentes seleccionadas

| Archivo local | Perfil | URL | Autor | MD5 esperado | Estado |
|---|---|---|---|---|---|
| `freepd_space_ambience.mp3` | ambiente electrónico, contenido sostenido | <https://archive.org/download/freepd/electronic/Space%20Ambience.mp3> | Alexander Nakarada | `e6c7d58626089af7825f77da784ad254` | pendiente de descarga |
| `freepd_backbeat.mp3` | ritmo electrónico y transitorios periódicos | <https://archive.org/download/freepd/electronic/Backbeat.mp3> | Kevin MacLeod | `dd5050452797cc97fd34cdb51f3d0b7b` | pendiente de descarga |
| `freepd_piano_magic_motive.mp3` | motivo tonal/piano | <https://archive.org/download/freepd/romantic/Piano%20Magic%20Motive.mp3> | Kevin MacLeod | `ee4080311f444e72d85a8e7e4bef1566` | pendiente de descarga |
| `freepd_improv_for_evil.mp3` | material experimental y menos tonal | <https://archive.org/download/freepd/misc/Improv%20for%20Evil.mp3> | Brian Boyko | `1de7233723ba3e2103ffb01c30012475` | pendiente de descarga |

## Procedencia y uso

- Colección archivada: <https://archive.org/details/freepd>.
- Metadatos reproducibles: <https://archive.org/metadata/freepd>.
- Listado de archivos: <https://archive.org/download/freepd/freepd_files.xml>.
- La colección FreePD se presenta como música de dominio público; el manifiesto conserva autor y hash para que una revisión legal pueda comprobar cada archivo individual.
- Los MP3 deben convertirse a WAV PCM lineal mediante `ffmpeg` antes de `host-tools/pack-pcm`; la conversión y los WAV resultantes permanecen en `out/`.

## Comprobación

Cuando estén disponibles, descargar y comprobar en PowerShell:

```powershell
Get-FileHash out/assets/audio/freepd/freepd_backbeat.mp3 -Algorithm MD5
```

En GNU-Bash:

```bash
md5sum out/assets/audio/freepd/*.mp3
```

La descarga realizada durante esta sesión recibió respuestas HTTP 500 del servidor de archivos, por lo que no se registran archivos como disponibles hasta superar esta comprobación.
