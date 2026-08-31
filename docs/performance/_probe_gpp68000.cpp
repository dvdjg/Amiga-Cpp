// Sonda reproducible para el doc OPTIMIZACION_GPP_68000.md (§0, §8).
// Compilación de referencia (igual que la evidencia del 2026-08-31):
//   m68k-amiga-elf-g++ -m68000 -std=gnu++23 -O2 -S _probe_gpp68000.cpp
//   m68k-amiga-elf-g++ -m68000 -std=gnu++23 -O2 -Os -S _probe_gpp68000.cpp
// Revisar el .s generado: buscar muls.w/divu.w/dbra/lsr.b (bitfield).
// NOTA: no usar -O0 (ICE del fork con shifts variables, ver §1).
// Este toolchain es freestanding: NO incluye <cstdint>; usar tipos sin signo.

using ui16 = unsigned short;
using i16 = signed short;
using ui32 = unsigned long;
using si32 = signed long;

// [✓] Mul/div 16-bit nativos: muls.w / divu.w.
i16 mul_short(i16 a, i16 b) { return static_cast<i16>(a * b); }
ui16 div_short(ui16 a, ui16 b) { return static_cast<ui16>(a / b); }

// [✓] Countdown + -Os => dbra. Con -O2 solo, GCC elige límite de puntero.
si32 sum_dbra(const i16* arr, i16 n) {
    i16 c = n;
    c = static_cast<i16>(c - 1);
    si32 acc = 0;
    do { acc += *arr++; } while (--c != static_cast<i16>(-1));
    return acc;
}

// Bucle hacia delante (control): NO emite dbra ni a -Os.
si32 sum_postinc(const i16* arr, i16 n) {
    si32 acc = 0;
    const i16* p = arr;
    for (i16 i = 0; i < n; ++i) acc += *p++;
    return acc;
}

// [✓] Bitfield vs máscara+shift: el bitfield sale más corto (lsr.b) y sin spill.
struct Flags4 { ui16 a : 4, b : 4, c : 4, d : 4; };
ui16 bitfield_get(const Flags4& f) { return static_cast<ui16>(f.a + f.c); }
ui16 mask_shift(ui16 w) { return static_cast<ui16>(((w & 0xf000u) >> 12) + ((w & 0x00f0u) >> 4)); }

// [✓] Variable local de registro (la única forma de "forzar" registro aquí).
si32 sum_local(const ui16* arr, ui16 n) {
    register const ui16* rp __asm("a0");
    rp = arr;
    si32 acc = 0;
    for (ui16 i = 0; i < n; ++i) acc += rp[i];
    return acc;
}