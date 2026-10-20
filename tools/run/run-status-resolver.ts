import * as fs from 'node:fs';

/** Sección ELF del `.map`; `start`/`end` son direcciones enlazadas, no runtime. */
export type LinkedMapSection = { name: string; start: number; end: number };

/** Resultado del mapeo estricto de `g_eng_run_status` al hunk `.data`. */
export type StrictDataHunkAddress = {
  hunkCount: number;
  dataIndex: number;
  dataBase: number;
  address: number;
};

/**
 * Interpreta HUNK_HEADER y devuelve la cantidad de hunks de carga.
 *
 * `bytes` es el contenido completo o parcial del ejecutable Amiga. Se validan
 * los nombres residentes y la tabla declarada; un encabezado inválido/truncado
 * devuelve 0. El runner usa el resultado para evitar búsquedas ambiguas en BSS.
 */
export function readHunkCountFromBytes(bytes: Buffer): number {
  try {
    if (!Buffer.isBuffer(bytes) || bytes.length < 12 || bytes.readUInt32BE(0) !== 0x3f3) return 0;

    // HUNK_HEADER: magic, nombres residentes (longitud en longs + bytes), cero final.
    let offset = 4;
    for (;;) {
      if (offset + 4 > bytes.length) return 0;
      const nameLongs = bytes.readUInt32BE(offset);
      offset += 4;
      if (nameLongs === 0) break;
      if (nameLongs > Math.floor((bytes.length - offset) / 4)) return 0;
      offset += nameLongs * 4;
    }

    // Después del terminador: table size, first/last hunk y una entrada por hunk.
    if (offset + 4 > bytes.length) return 0;
    const tableSize = bytes.readUInt32BE(offset);
    if (tableSize < 1 || offset + (3 + tableSize) * 4 > bytes.length) return 0;
    return tableSize;
  } catch {
    return 0;
  }
}

/**
 * Lee HUNK_HEADER desde disco y devuelve el número de hunks, o 0 si no es fiable.
 * `exePath` es la ruta host del HUNK que el runner va a cargar en `dh1:a.exe`.
 */
export function readHunkCount(exePath: string): number {
  try {
    if (!exePath || !fs.existsSync(exePath)) return 0;
    return readHunkCountFromBytes(fs.readFileSync(exePath));
  } catch {
    return 0;
  }
}

/**
 * Indica si el runner debe usar el mapeo estricto de A1200.
 * Solo el valor literal `"1"` activa la ruta; el A500 conserva el fallback normal.
 */
export function isStrictReadyEnabled(value: string | undefined): boolean {
  return value === '1';
}

function runtimeAddress(value: string | number | undefined): number {
  if (typeof value === 'number') return value;
  if (typeof value !== 'string') return 0;
  const text = value.trim();
  return parseInt(text.startsWith('0x') || text.startsWith('0X') ? text.slice(2) : text, 16) || 0;
}

/**
 * Resuelve un símbolo enlazado que pertenece a `.data` a su dirección runtime.
 * En estos HUNK de amiga-gcc, `.data` es el penúltimo hunk y `.bss` el último.
 * Devuelve null si faltan hunks/secciones o si el símbolo no está en `.data`.
 */
export function resolveStrictDataHunkAddress(
  linkedSymbol: number,
  mapSections: LinkedMapSection[],
  runtimeSections: Array<string | number> | undefined,
  hunkCount: number,
): StrictDataHunkAddress | null {
  if (!Array.isArray(runtimeSections) || hunkCount < 2) return null;

  const dataIndex = hunkCount - 2;
  if (dataIndex >= runtimeSections.length) return null;
  const dataBase = runtimeAddress(runtimeSections[dataIndex]);
  if (!dataBase) return null;

  const dataSection = mapSections.find(
    (section) => section.name === '.data' && linkedSymbol >= section.start && linkedSymbol < section.end,
  );
  if (!dataSection) return null;

  return {
    hunkCount,
    dataIndex,
    dataBase,
    address: dataBase + (linkedSymbol - dataSection.start),
  };
}
