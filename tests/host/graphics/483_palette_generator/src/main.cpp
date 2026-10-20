#include <eng/graphics/palette_generator.hpp>

#include <cstdio>

namespace {

constexpr auto kNormal3 = eng::graphics::generate_palette<3u>({
	.transparent_index_zero = false,
});
constexpr auto kNormal4 = eng::graphics::generate_palette<4u>();
constexpr auto kNormal5 = eng::graphics::generate_palette<5u>();
constexpr auto kEhb = eng::graphics::generate_palette<5u, eng::graphics::PaletteMode::Ehb>();

static_assert(kNormal3.size() == 8u);
static_assert(kNormal4.size() == 16u);
static_assert(kNormal5.size() == 32u);
static_assert(kEhb.size() == 64u);
static_assert(kNormal5[0u] == 0u);
static_assert(kEhb[0u] == 0u && kEhb[32u] == 0u);

template <eng::usize Capacity>
bool unique(const eng::util::Array<eng::u16, Capacity>& palette, eng::usize first,
	    eng::usize end) {
	for (eng::usize i = first; i < end; ++i) {
		for (eng::usize j = i + 1u; j < end; ++j) {
			if (palette[i] == palette[j]) { return false; }
		}
	}
	return true;
}

} // namespace

int main() {
	if (!unique(kNormal3, 0u, 8u) || !unique(kNormal4, 1u, 16u)) {
		std::puts("reduced palette contains a duplicate");
		return 1;
	}
	if (!unique(kNormal5, 1u, 32u)) {
		std::puts("palette 5bpp contains a duplicate");
		return 1;
	}
	if (kNormal5[1u] != 0u || kNormal5[31u] != 0x0fffu) {
		std::puts("palette endpoints were not preserved");
		return 1;
	}
	for (eng::usize i = 0u; i < 32u; ++i) {
		const eng::u16 base = kEhb[i];
		const eng::u16 expected = static_cast<eng::u16>((((base >> 8u) & 15u) >> 1u) << 8u |
							 (((base >> 4u) & 15u) >> 1u) << 4u |
							 ((base & 15u) >> 1u));
		if (kEhb[32u + i] != expected) {
			std::puts("EHB half-bright derivation mismatch");
			return 1;
		}
	}
	std::puts("HOST-483 palette_generator: OK");
	return 0;
}
