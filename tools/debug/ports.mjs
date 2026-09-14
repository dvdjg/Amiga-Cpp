#!/usr/bin/env node
// Puertos de WinUAE para multi-instancia (respeto entre sesiones/agentes).
//
// El fork WinUAE-DBG lee los puertos del entorno: WINUAE_GDB_PORT (GDB, 2345) y
// WINUAE_SIDE_CHANNEL_PORT (canal lateral, 2346). Estos helpers evitan pisar la
// instancia de otro: usan el entorno si esta fijado, o eligen un par libre.
import net from 'node:net';

export const GDB_PORT = parseInt(process.env.WINUAE_GDB_PORT || '2345', 10);
export const SIDE_PORT = parseInt(process.env.WINUAE_SIDE_CHANNEL_PORT || '2346', 10);

function portFree(port) {
  return new Promise((resolve) => {
    const s = net.createServer();
    s.once('error', () => resolve(false));
    s.once('listening', () => s.close(() => resolve(true)));
    s.listen(port, '127.0.0.1');
  });
}

/// Busca un par (GDB, lateral) libre a partir de `base` (pasos de 2).
export async function pickFreePorts(base = 2345) {
  for (let p = base; p < base + 200; p += 2) {
    if ((await portFree(p)) && (await portFree(p + 1))) return { gdb: p, side: p + 1 };
  }
  throw new Error('sin par de puertos libre');
}

/// Puerto GDB/lateral efectivos: si los del entorno estan ocupados, elige otros y
/// los EXPORTA (WinUAE los lee del entorno al lanzarse).
export async function ownPorts() {
  if ((await portFree(GDB_PORT)) && (await portFree(SIDE_PORT))) {
    return { gdb: GDB_PORT, side: SIDE_PORT };
  }
  const p = await pickFreePorts(2350);
  process.env.WINUAE_GDB_PORT = String(p.gdb);
  process.env.WINUAE_SIDE_CHANNEL_PORT = String(p.side);
  return p;
}
