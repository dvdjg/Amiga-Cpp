// ============================================================================
// Test HOST-002: vocabulario portátil de intenciones (Visual / CopperIntent /
// SpriteIntent / concept Effect)
// ============================================================================
//
// Valida en host la nueva estructura de tipos del engine definida en
// `eng/graphics/raster_intent.hpp`: los tipos que capturan LO QUE la escena
// quiere, sin decidir CÓMO se materializa en hardware.
//
// Comprobaciones:
//   1) Static asserts de que `Visual`, `CopperIntent`, `SpriteIntent` cumplen
//      ser trivialmente copiables y auto-contenidos (portables, host-testables).
//   2) concept `Effect<E, Plan>`: un efecto mínimo que solo tiene `update()` y
//      `apply_into(plan)` lo satisface; uno que falta no debe compilar (se
//      comprueba asumiendo que NO se usa el negativo, para no romper el build).
//   3) Runtime: `CopperIntent` anotado por franja conserva su geometría.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/002_raster_intent   (solo este)
//   bash tools/run-host-tests.sh                                (todos)

#include <cstdio>
#include <type_traits>

#include <eng/core/types.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite.hpp>
#include <eng/graphics/copper/scheduler.hpp>

namespace {

using eng::graphics::CopperIntent;
using eng::graphics::CopperIntentKind;
using eng::graphics::Effect;
using eng::graphics::SpriteIntent;
using eng::graphics::SpriteTemplate;
using eng::graphics::Visual;
using eng::graphics::VisualKind;
using eng::MemoryBlock;
using eng::MemoryKind;

// Los tipos son datos planos (POD) que viajan entre la escena y los schedulers.
static_assert(std::is_trivially_copyable_v<Visual>);
static_assert(std::is_trivially_copyable_v<CopperIntent>);
static_assert(std::is_trivially_copyable_v<SpriteIntent>);
static_assert(std::is_trivially_copyable_v<SpriteTemplate<4, 4>>);

// Un "plan" mínimo para el concept `Effect`: en el engine real es `FramePlan`.
struct MockPlan {
    int copper_intents = 0;
};

// Efecto mínimo que satisface `Effect` (update(tick) + apply_into).
struct MockCycler {
    int phase = 0;
    void update(eng::u16 frame_index) { phase = static_cast<int>(frame_index) + 1; }
    void apply_into(MockPlan& plan) {
        plan.copper_intents += phase;  // aporta su estado al plan
    }
};

// Evidencia del contract: MockCycler cumple `Effect<_, MockPlan>` (Tick = u16 por defecto).
static_assert(Effect<MockCycler, MockPlan>);

// `VisualKind` es distinguible y compacto (u8).
static_assert(sizeof(VisualKind) == 1);

} // namespace

int main() {
    // Runtime: la geometría de una franja de `CopperIntent` se conserva.
    CopperIntent intent {
        CopperIntentKind::PaletteLine,
        /*top*/ 44, /*bottom*/ 44,
        /*hpos*/ 0, /*colors*/ nullptr, /*first*/ 0, /*count*/ 0,
        /*shift_x*/ 0, /*bitplanes*/ nullptr,
        /*sprite_channel*/ 0, /*sprite_ptr*/ nullptr,
    };
    if (intent.top != 44 || intent.bottom != 44 || intent.kind != CopperIntentKind::PaletteLine) {
        std::printf("[FAIL] geografia de CopperIntent incorrecta\n");
        return 1;
    }

    // Runtime: un `Visual` tipo Bob con mascara conserva su identidad de contenido.
    Visual v {};
    v.kind = VisualKind::Bob;
    v.w = 32;
    v.h = 32;
    v.bitplanes = 4;
    if (v.kind != VisualKind::Bob || v.w != 32 || v.bitplanes != 4) {
        std::printf("[FAIL] Visual incorrecto\n");
        return 1;
    }

    // Runtime: un efecto avanza y aporta intenciones.
    MockPlan plan {};
    MockCycler cycler {};
    cycler.update(1);
    cycler.update(3);
    cycler.apply_into(plan);
    if (plan.copper_intents != 4) {
        std::printf("[FAIL] Effect no aporta intenciones (%d)\n", plan.copper_intents);
        return 1;
    }

    // Runtime: una SpriteTemplate trocea la imagen y respeta su límite fijo sin heap.
    SpriteTemplate<4, 4> tpl {};
    tpl.width_words = 1;
    tpl.add_segment({0, 8, 0});
    tpl.add_segment({16, 8, 8});
    if (tpl.segment_count != 2 || tpl.segments[1].height != 8) {
        std::printf("[FAIL] SpriteTemplate no trocea la imagen\n");
        return 1;
    }
    for (int i = 0; i < 10; ++i) tpl.add_segment({0, 1, 0});
    if (tpl.segment_count != 4) {
        std::printf("[FAIL] SpriteTemplate no respeta MaxSegments (segment_count=%d)\n", (int)tpl.segment_count);
        return 1;
    }

    // Runtime: el scheduler base materializa las intents de paleta y marca las que
    // requieren contexto de display como "sin manejar" (telemetria honrada).
    {
        eng::u16 copper_words[128] {};
        MemoryBlock copper_block { copper_words, sizeof(copper_words), MemoryKind::Chip };
        eng::copper::Scheduler sched { copper_block };

        eng::u16 fake_palette[32] {};
        fake_palette[1] = 0xf00;
        CopperIntent intents[3] {
            { CopperIntentKind::PaletteLine,  44,  44,  0, fake_palette, 1, 7, 0, nullptr, 0, nullptr },
            { CopperIntentKind::ShiftLines,  100, 100,  0,      nullptr, 0, 0, 4, nullptr, 0, nullptr },
            { CopperIntentKind::PaletteLine,  90,  90,  0, fake_palette, 0, 1, 0, nullptr, 0, nullptr },
        };

        sched.emit_copper_intents(intents, 3);
        sched.end();

        const auto& rep = sched.report();
        if (!rep.ok) {
            std::printf("[FAIL] scheduler no ok con plan de paleta\n");
            return 1;
        }
        // 7 + 1 movimientos de color emitidos y WAITs: la paleta se materializo.
        if (rep.palette_moves < 8u || rep.waits < 2u) {
            std::printf("[FAIL] scheduler no emitio paleta/aciertos (moves=%d waits=%d)\n",
                        (int)rep.palette_moves, (int)rep.waits);
            return 1;
        }
        // ShiftLines requiere contexto de display: se marca, no se emite falsamente.
        if (rep.unhandled_intents != 1u) {
            std::printf("[FAIL] scheduler no marco ShiftLines como sin manejar (%d)\n",
                        (int)rep.unhandled_intents);
            return 1;
        }
    }

    std::printf("OK: vocabulario de intenciones validado (Visual/CopperIntent/SpriteIntent/SpriteTemplate/Effect/scheduler).\n");
    return 0;
}
