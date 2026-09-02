// Permutación de export EHB: convierte la paleta/índices INTERCALADOS
// ([base0,half0,base1,half1,...], con slot transparente opcional al inicio) a la
// convención BASES-PRIMERO que consume el chipset EHB (COLOR00..31 = bases,
// 32..63 = half). El Amiga NO debe transformar píxeles en CPU (regla 7), así que
// esta reindexación se hace siempre en el HOST, aquí, y `slice-tiles.mjs` la aplica
// en su export (paso 6). `expIndex` es una biyección (1:1) sobre 0..palSize-1, no
// idempotente: solo se aplica una vez a los índices intercalados internos.
//
// Uso: const { expPalette, expIndex } = buildExport(paletteI, hasAlphaSrc);
export function buildExport(paletteI, hasAlphaSrc) {
  const palSize = paletteI.length;
  const expT = hasAlphaSrc ? 1 : 0;            // slot transparente reservado
  const expNB = (palSize - expT) / 2;          // nº de bases (= nº de half)
  const expPalette = new Array(palSize);
  for (let i = 0; i < expT; i++) expPalette[i] = [...paletteI[i]];
  for (let k = 0; k < expNB; k++) {
    expPalette[expT + k] = [...paletteI[expT + 2 * k]];            // base k -> pos expT+k
    expPalette[expT + expNB + k] = [...paletteI[expT + 2 * k + 1]]; // half k -> expT+nB+k
  }
  const expIndex = (v) => {
    if (v < expT) return v;                    // transparente se queda
    const u = v - expT;                        // 0..2*expNB-1, intercalado
    return (u & 1) === 0 ? expT + (u >> 1) : expT + expNB + ((u - 1) >> 1);
  };
  return { expPalette, expIndex };
}
