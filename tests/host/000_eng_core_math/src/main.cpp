// ============================================================================
// Test HOST-000: eng::core math (isqrt + sort) — port de libmisc de
// demoscene-repo-orig.
// ============================================================================
//
// Este test se compila con g++ del host (sin WinUAE) contra engine/include y
// valida dos algoritmos portados de `demoscene-repo-orig/lib/libmisc`:
//
//   - eng::core::isqrt   (fx.c  : isqrt por tabla, sin división ni floats)
//   - eng::core::sort    (sort.c: quick sort + insertion sort)
//
// Ambos son freestanding (no usan STL ni backend); por eso pueden probarse en
// host de forma rápida y determinista. El main.cpp SÍ usa printf del host solo
// para informar del resultado.
//
// IMPORTANTE sobre isqrt: el algoritmo original NO es una raíz exacta; es una
// aproximación por tabla que subestima (p. ej. isqrt(9)==2, isqrt(100)==10,
// isqrt(32768)==181). Se han autenticado las muestras de abajo contra el C
// original compilado, de modo que el test valida EQUIVALENCIA con el port fiel,
// no la propiedad matemática perfecta.
//
// Ejecución:
//   bash tools/run-host-tests.sh            (compila y corre todos)
//   bash tools/run-host-tests.sh tests/host/000_eng_core_math   (solo este)

#include <cstdio>

#include <eng/core/crc32.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/random.hpp>
#include <eng/core/sort.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/utf8.hpp>
#include <eng/graphics/font8.hpp>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                      \
            std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);       \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

void test_isqrt_matches_original() {
    std::printf("isqrt: equivalencia con el C original (muestras autenticadas)\n");

    // (n, isqrt(n) del C original compilado). Valores clave para detectar el
    // sesgo por borde de tabla y los cuadrados.
    struct {
        eng::u32 n;
        eng::u32 expect;
    } const cases[] = {
        { 0u, 0u },           { 1u, 1u },           { 2u, 1u },
        { 3u, 1u },           { 4u, 2u },           { 8u, 2u },
        { 9u, 2u },           { 10u, 2u },          { 15u, 2u },
        { 16u, 4u },          { 25u, 5u },          { 36u, 5u },
        { 48u, 6u },          { 49u, 6u },          { 63u, 7u },
        { 64u, 8u },          { 81u, 8u },          { 100u, 10u },
        { 121u, 10u },        { 144u, 11u },        { 169u, 12u },
        { 196u, 13u },        { 225u, 14u },        { 256u, 16u },
        { 289u, 16u },        { 324u, 17u },        { 361u, 18u },
        { 400u, 20u },        { 441u, 20u },        { 484u, 21u },
        { 529u, 22u },        { 576u, 23u },        { 625u, 24u },
        { 676u, 25u },        { 729u, 26u },        { 784u, 27u },
        { 841u, 28u },        { 900u, 29u },        { 961u, 30u },
        { 1024u, 32u },       { 32768u, 181u },     { 65535u, 253u },
        { 65536u, 256u },     { 100000u, 313u },    { 200000u, 443u },
        { 1000000u, 999u },   { 2147483647u, 45977u },
    };

    for (const auto& c : cases) {
        const eng::u32 got = eng::isqrt(c.n);
        if (got != c.expect) {
            std::printf("  [FAIL] isqrt(%lu) == %lu, se esperaba %lu\n",
                        static_cast<unsigned long>(c.n),
                        static_cast<unsigned long>(got),
                        static_cast<unsigned long>(c.expect));
            ++g_failures;
        }
    }
}

void test_crc32_matches_original() {
    std::printf("crc32: equivalencia con el C original (muestras autenticadas)\n");

    // (buffer, len, crc32 del C original compilado). El valor de "123456789"
    // (0xCBF43926) es el CRC-32 estándar de la spec, y el resto se autenticaron
    // compilando libmisc/crc32.c contra estos buffers.
    const eng::u8 b1[] = { '1','2','3','4','5','6','7','8','9' };
    const eng::u8 b2[] = { 0x00 };
    const eng::u8 b3[] = { 0x00, 0x00, 0x00, 0x00 };
    const eng::u8 b4a[] = { 'a','b','c','d','e','f','g','h','i','j','k','l','m',
                            'n','o','p','q','r','s','t','u','v','w','x','y','z' };
    const eng::u8 b5[] = { 0x41 };
    const eng::u8 b6[] = { 'T','h','e',' ','q','u','i','c','k',' ','b','r','o','w','n',
                           ' ','f','o','x',' ','j','u','m','p','s',' ','o','v','e','r',' ',
                           't','h','e',' ','l','a','z','y',' ','d','o','g' };

    struct {
        const eng::u8* data;
        eng::u32 len;
        eng::u32 expect;
    } const cases[] = {
        { b1, sizeof(b1), 0xCBF43926u },
        { b2, sizeof(b2), 0xD202EF8Du },
        { b3, sizeof(b3), 0x2144DF1Cu },
        { b4a, sizeof(b4a), 0x4C2750BDu },
        { b5, sizeof(b5), 0xD3D99E8Bu },
        { b6, sizeof(b6), 0x414FA339u },
    };

    for (const auto& c : cases) {
        const eng::u32 got = eng::crc32(c.data, c.len);
        if (got != c.expect) {
            std::printf("  [FAIL] crc32(len=%lu) == 0x%08lx, se esperaba 0x%08lx\n",
                        static_cast<unsigned long>(c.len),
                        static_cast<unsigned long>(got),
                        static_cast<unsigned long>(c.expect));
            ++g_failures;
        }
    }
}

void test_random_matches_original() {
    std::printf("random: xoroshiro64++ (equivalente al random.c del demoscene con swap)\n");

    // Estado inicial s0=1,s1=0, primeros 10 outputs. La secuencia coincide con
    // la del random.c original (cuyo rol por rangos+swap16 equivale a rotl32),
    // autenticada compilando el C del origen.
    eng::Xoroshiro64pp rng{1u, 0u};
    const eng::u32 expect[] = {
        0x48020a01u, 0x81662931u, 0xcd2b5253u, 0xd3e6cbe6u, 0xcd5af43du,
        0x860aa4bau, 0xb7bea7fbu, 0x63dcaff3u, 0x762d74c9u, 0x3e7d7e8fu,
    };
    for (const eng::u32 e : expect) {
        const eng::u32 got = rng.next();
        if (got != e) {
            std::printf("  [FAIL] random() == 0x%08lx, se esperaba 0x%08lx\n",
                        static_cast<unsigned long>(got),
                        static_cast<unsigned long>(e));
            ++g_failures;
        }
    }
}

void test_font8() {
    std::printf("font8: glifos presentes para caracteres usados por la demo 060\n");

    // La demo dibuja "Demo 060 - eng::core self-check OK/FAIL isqrt crc32
    // random sort SELF-CHECK : ALL PHASES" — verifica que esos glifos tienen
    // al menos un bit encendido (no estan vacios). El espacio (0x20) se
    // excluye: es legítimamente todo ceros.
    const char* needed = "Demo060-eng::coreself-checkOK/FAILisqrtcrc32random"
                         "sortSELF-CHECK:ALLPHASES123456789!.";
    for (const char* p = needed; *p; ++p) {
        const eng::u16 ch = static_cast<eng::u16>(static_cast<unsigned char>(*p));
        bool any = false;
        for (eng::u8 row = 0; row < eng::Font8::kRows; ++row) {
            if (eng::Font8::row(ch, row) != 0) {
                any = true;
                break;
            }
        }
        if (!any) {
            std::printf("  [FAIL] fuente sin glifo para '%c' (0x%02X)\n", *p, ch);
            ++g_failures;
        }
    }
}

void test_utf8() {
    std::printf("utf8: decodificacion de secuencias LATIN-1 (acentos/ñ/ü)\n");

    // Literales UTF-8: "á é í ó ú ñ ü ç" con sus code points LATIN-1.
    const char* s1 = "\xC3\xA1"; // á UTF-8 (0xE1)
    const char* s2 = "\xC3\xA9"; // é (0xE9)
    const char* s3 = "\xC3\xAD"; // í (0xED)
    const char* s4 = "\xC3\xB1"; // ñ (0xF1)
    const char* s5 = "\xC3\x9C"; // Ü (0xDC)
    const char* s6 = "\xC3\xBC"; // ü (0xFC)
    const char* s7 = "\xC3\x87"; // Ç (0xC7)
    const char* s8 = "Hola";     // ASCII puro

    const eng::u8* p = reinterpret_cast<const eng::u8*>(s1);
    CHECK(eng::utf8::decode(p) == 0xE1u);
    const eng::u8* q = reinterpret_cast<const eng::u8*>(s2);
    CHECK(eng::utf8::decode(q) == 0xE9u);
    const eng::u8* r = reinterpret_cast<const eng::u8*>(s3);
    CHECK(eng::utf8::decode(r) == 0xEDu);
    const eng::u8* t = reinterpret_cast<const eng::u8*>(s4);
    CHECK(eng::utf8::decode(t) == 0xF1u);
    const eng::u8* u = reinterpret_cast<const eng::u8*>(s5);
    CHECK(eng::utf8::decode(u) == 0xDCu);
    const eng::u8* v = reinterpret_cast<const eng::u8*>(s6);
    CHECK(eng::utf8::decode(v) == 0xFCu);
    const eng::u8* w = reinterpret_cast<const eng::u8*>(s7);
    CHECK(eng::utf8::decode(w) == 0xC7u);
    const eng::u8* x = reinterpret_cast<const eng::u8*>(s8);
    CHECK(eng::utf8::decode(x) == 'H');

    // Y que esos code points tienen glifo en Font8 (no vacíos).
    CHECK(eng::Font8::row(0xE1u, 2) != 0u); // á
    CHECK(eng::Font8::row(0xF1u, 2) != 0u); // ñ
    CHECK(eng::Font8::row(0xFCu, 2) != 0u); // ü
}

void test_utf8_literal_constexpr() {
    std::printf("utf8: fixed_string NTTP decodifica en compile-time\n");

    // "Hola" + á é ñ ü (8 code points). El fuente es UTF-8.
    constexpr auto d1 = eng::utf8::decode_fixed<"Holaáéñü">();
    static_assert(d1.count == 8u, "Hola+áéñü = 8 code points");
    static_assert(d1.cp[0] == 'H', "H");
    static_assert(d1.cp[4] == 0xE1u, "á");
    static_assert(d1.cp[5] == 0xE9u, "é");
    static_assert(d1.cp[6] == 0xF1u, "ñ");
    static_assert(d1.cp[7] == 0xFCu, "ü");
    CHECK(eng::utf8::decode_fixed<"Você">().count == 4u);
    CHECK(eng::utf8::decode_fixed<"Você">().cp[3] == 0xEAu); // ê
}

void test_row_bytes_consistency() {
    std::printf("font8: tamano de la tabla coherente\n");
    // La fuente esta en formato FILAS (byte r = fila r, bit k = pixel en la
    // columna k desde la izquierda). Comprobamos extremos del rango.
    CHECK(eng::Font8::row(' ', 0) == 0u);       // espacio en blanco
    CHECK(eng::Font8::row('A', 0) != 0u);       // 'A' fila superior
    CHECK(eng::Font8::row('0', 0) != 0u);       // '0'
    CHECK(eng::Font8::row('-', 3) != 0u);       // '-' en fila central
}

void test_sort_ints() {
    std::printf("sort: quick_sort sobre ints\n");

    eng::s32 a[] = { 9, 4, 7, 1, 5, 3, 8, 2, 6, 0, 10, -3 };
    const eng::s32 n = static_cast<eng::s32>(sizeof(a) / sizeof(a[0]));
    eng::quick_sort(eng::Span<eng::s32>{a}, [](eng::s32 x, eng::s32 y) {
        return x <= y;
    });

    for (eng::s32 i = 1; i < n; ++i) {
        CHECK(a[i - 1] <= a[i]);
    }
    // La ordenación permuta: el multiconjunto se conserva.
    CHECK(a[0] == -3);
    CHECK(a[n - 1] == 10);
}

void test_sort_items() {
    std::printf("sort: sort_items ordena por key (quicksort no estable)\n");

    eng::SortItem items[] = {
        { 3, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 1, 4 }, { 2, 5 },
    };
    const auto n = static_cast<eng::usize>(sizeof(items) / sizeof(items[0]));
    eng::sort_items(eng::Span<eng::SortItem>{items});

    // Ordenadas por key (el orden de los iguales no está garantizado).
    for (eng::usize i = 1; i < n; ++i) {
        CHECK(items[i - 1].key <= items[i].key);
    }
}

} // namespace

int main() {
    std::printf("Test HOST-000 eng_core_math (isqrt + sort)\n");
    std::printf("==========================================\n");

    test_isqrt_matches_original();
    test_crc32_matches_original();
    test_random_matches_original();
    test_font8();
    test_utf8();
    test_utf8_literal_constexpr();
    test_row_bytes_consistency();
    test_sort_ints();
    test_sort_items();

    if (g_failures == 0) {
        std::printf("OK: todas las comprobaciones pasaron\n");
        return 0;
    }
    std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
    return 1;
}