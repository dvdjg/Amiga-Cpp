#!/usr/bin/env node
// tar-extract.mjs: extrae un archivo .tar (POSIX ustar/pax/GNU basicos) a un directorio, para
// preparar el contenido de un volumen Amiga desde una fuente que se empaqueta con la orden
// `tar` estandar (ASI el "archivo original" puede contener un sistema de archivos).
//
// Uso:
//   tar -cf assets.tar -C out/assets/volume .
//   node tools/fs/tar-extract.mjs assets.tar out/tmp/volume
//   # y luego construir el ADF con tools/fs/make-volume.mjs (o --disk del runner)
//
// Soporta: ficheros regulares ('0'/'\0'), directorios ('5'), GNU LongName ('L')/LongLink ('K'),
// pax extended headers ('x'/'g', se ignoran sus atributos) y enlaces simbolicos/duros ('2'/'1',
// se omiten). No sigue enlaces; comprueba que las rutas no escapen del destino.
import fs from 'node:fs';
import path from 'node:path';

const BLOCK = 512;

function parseOctal(buf) {
  const s = buf.toString('latin1').replace(/\0.*$/, '').trim();
  return s === '' ? 0 : parseInt(s, 8);
}

export function extractTar(tarPath, destDir) {
  const data = fs.readFileSync(tarPath);
  const root = path.resolve(destDir);
  fs.mkdirSync(root, { recursive: true });
  let off = 0;
  let longName = null;
  let count = 0;

  while (off + BLOCK <= data.length) {
    const header = data.subarray(off, off + BLOCK);
    if (header.every((b) => b === 0)) {
      break; // fin: bloque(s) de ceros
    }
    const nameField = header.subarray(0, 100).toString('utf8').replace(/\0.*$/, '');
    const size = parseOctal(header.subarray(124, 136));
    const type = String.fromCharCode(header[156] || 0x30);
    const prefix = header.subarray(345, 500).toString('utf8').replace(/\0.*$/, '');
    let name = prefix ? `${prefix}/${nameField}` : nameField;
    off += BLOCK;

    const body = data.subarray(off, off + size);
    off += Math.ceil(size / BLOCK) * BLOCK;
    if (off > data.length) off = data.length;

    if (type === 'L') {
      longName = body.toString('utf8').replace(/\0.*$/, '');
      continue;
    }
    if (type === 'K' || type === 'x' || type === 'g') {
      continue; // metadatos: no afectan al contenido
    }
    if (longName !== null) {
      name = longName;
      longName = null;
    }

    const target = path.resolve(root, name);
    if (target !== root && !target.startsWith(root + path.sep)) {
      throw new Error(`ruta fuera del destino: ${name}`);
    }
    if (type === '5') {
      fs.mkdirSync(target, { recursive: true });
      continue;
    }
    if (type !== '0' && type !== '\0') {
      continue; // enlaces/dispositivos: se omiten
    }
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, body);
    count++;
  }
  return count;
}

if (import.meta.url === `file://${process.argv[1]}` ||
    process.argv[1]?.endsWith('tar-extract.mjs')) {
  const [tarPath, destDir] = process.argv.slice(2);
  if (!tarPath || !destDir) {
    console.error('uso: node tools/fs/tar-extract.mjs <archivo.tar> <directorio-destino>');
    process.exit(2);
  }
  const n = extractTar(tarPath, destDir);
  console.log(`tar-extract: ${n} ficheros -> ${destDir}`);
}
