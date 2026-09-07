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

#include <eng/core/isqrt.hpp>
#include <eng/core/sort.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

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
    test_sort_ints();
    test_sort_items();

    if (g_failures == 0) {
        std::printf("OK: todas las comprobaciones pasaron\n");
        return 0;
    }
    std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
    return 1;
}