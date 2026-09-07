// Sonda reproducible para el doc OPTIMIZACION_GPP_68000.md (Â§0, Â§8).
// CompilaciÃ³n de referencia (igual que la evidencia del 2026-08-31):
//   m68k-amiga-elf-g++ -m68000 -std=gnu++23 -O2 -S _probe_gpp68000.cpp
//   m68k-amiga-elf-g++ -m68000 -std=gnu++23 -O2 -Os -S _probe_gpp68000.cpp
// Revisar el .s generado: buscar muls.w/divu.w/dbra/lsr.b (bitfield).
// NOTA: no usar -O0 (ICE del fork con shifts variables, ver Â§1).
// Este toolchain es freestanding: NO incluye <cstdint>; usar tipos sin signo.

using ui16 = unsigned short;
using i16 = signed short;
using ui32 = unsigned long;
using si32 = signed long;

// [âœ“] Mul/div 16-bit nativos: muls.w / divu.w.
i16 mul_short(i16 a, i16 b) { return static_cast<i16>(a * b); }
ui16 div_short(ui16 a, ui16 b) { return static_cast<ui16>(a / b); }

// [âœ“] Countdown + -Os => dbra. Con -O2 solo, GCC elige lÃ­mite de puntero.
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

// [âœ“] Bitfield vs mÃ¡scara+shift: el bitfield sale mÃ¡s corto (lsr.b) y sin spill.
struct Flags4 { ui16 a : 4, b : 4, c : 4, d : 4; };
ui16 bitfield_get(const Flags4& f) { return static_cast<ui16>(f.a + f.c); }
ui16 mask_shift(ui16 w) { return static_cast<ui16>(((w & 0xf000u) >> 12) + ((w & 0x00f0u) >> 4)); }

// [âœ“] Variable local de registro (la Ãºnica forma de "forzar" registro aquÃ­).
si32 sum_local(const ui16* arr, ui16 n) {
    register const ui16* rp __asm("a0");
    rp = arr;
    si32 acc = 0;
    for (ui16 i = 0; i < n; ++i) acc += rp[i];
    return acc;
}

// ---------------------------------------------------------------------------
// Ports de libmisc (eng::core) validados por test host: comprobar que en 68000
// no emiten __divsi3/__mulsi3 en el hot path (regla de AGENTS: comparar el asm).
// Las funciones de abajo duplican en local el contenido de isqrt.hpp/sort.hpp/
// crc32.hpp/random.hpp para que el probe compile standalone (sin include).
// ---------------------------------------------------------------------------

// [P] isqrt (tabla + nlz, sin division): debe salir sin __udivsi3/__mulsi3.
static const ui16 kSqrt[65] = {
    0,1,1,2,2,4,5,8,11,16,22,32,45,64,90,128,181,256,362,512,724,1024,1448,2048,
    2896,4096,5792,8192,11585,16384,23170,32768,46340,32768,33276,33776,34269,34755,
    35235,35708,36174,36635,37090,37540,37984,38423,38858,39287,39712,40132,40548,
    40960,41367,41771,42170,42566,42959,43347,43733,44115,44493,44869,45241,45611,45977,
};
// nlz: cuenta ceros iniciales de 32 bits (sin tabla, para el probe).
static i16 nlz_probe(ui32 x) {
    i16 n = 0;
    if (x == 0u) return 32;
    if ((x & 0xFFFF0000u) == 0u) { n += 16; x <<= 16; }
    if ((x & 0xFF000000u) == 0u) { n += 8; x <<= 8; }
    if ((x & 0xF0000000u) == 0u) { n += 4; x <<= 4; }
    if ((x & 0xC0000000u) == 0u) { n += 2; x <<= 2; }
    if ((x & 0x80000000u) == 0u) { n += 1; }
    return n;
}
ui32 probe_isqrt(ui32 v) {
    ui16 cnt = nlz_probe(v);
    ui32 t;
    if (cnt <= 6u) t = v << (6u - cnt); else t = v >> (cnt - 6u);
    t &= 31u; t += 33u;
    t = (static_cast<ui32>(kSqrt[cnt]) * static_cast<ui32>(kSqrt[t])) << 1u;
    return t >> 16u;
}

// [P] crc32 (tabla de 256, bucle por byte): debe salir sin mul/div runtime.
static const ui32 kCrcTable[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};
ui32 probe_crc32(const unsigned char* data, ui32 len) {
    ui32 crc = 0xFFFFFFFFu;
    while (len--) crc = kCrcTable[(*data++ ^ static_cast<unsigned char>(crc))] ^ (crc >> 8u);
    return ~crc & 0xFFFFFFFFu;
}

// [P] quick_sort sobre Span-like (quicksort + insercion): sin mul/div runtime.
typedef struct { i16 key; i16 index; } ProbeSortItem;
static inline void probe_sort_swap(ProbeSortItem* a, ProbeSortItem* b) {
    ProbeSortItem t = *a; *a = *b; *b = t;
}
void probe_sort_insertion(ProbeSortItem* items, i16 first, i16 last) {
    i16 i;
    for (i = first + 1; i <= last; ++i) {
        ProbeSortItem this_ = items[i];
        i16 j = i;
        while (j > first && this_.key <= items[j - 1].key) { items[j] = items[j - 1]; --j; }
        items[j] = this_;
    }
}
void probe_sort_items(ProbeSortItem* items, i16 size) {
    if (size < 2) return;
    i16 lo = 0, hi = size - 1;
    ProbeSortItem pivot;
    /* particion simple (Lomuto) para el probe */
    for (;;) {
        i16 i = lo, j = lo;
        pivot = items[hi];
        for (; j < hi; ++j) if (items[j].key <= pivot.key) { probe_sort_swap(&items[i], &items[j]); ++i; }
        probe_sort_swap(&items[i], &items[hi]);
        /* recurrencia a la mitad izquierda; la derecha se ignora (probe) */
        if (i > lo) hi = i - 1; else break;
    }
}
void probe_drain(ProbeSortItem* items, i16 n) {
    (void)items; (void)n; // evita que el enlazador descarte las funciones
}
