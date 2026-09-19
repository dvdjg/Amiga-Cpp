#pragma once

/// \file expression.hpp
/// **Expresión no verbal** (`eng::sim`, capa de persona): los canales y gestos con que un
/// personaje **filtra** su estado interno, y el cálculo determinista de la **fuga**.
///
/// Un personaje no elige sus gestos: intenta controlarlos y se le escapan según su
/// compostura y la dificultad de control de cada canal. Los canales **autonómicos**
/// (pupilas, temblor, aleteo nasal, trago, pestañeo rápido) tienen control bajo y delatan
/// siempre la activación; los **volitivos** (sonrisa, postura, desdén) se controlan mejor.
///
/// `leak()` combina la emoción (`Mind`), la compostura efectiva y el control del gesto:
///
/// ```text
///   exceso      = max(0, afecto − umbral)                          (0..255)
///   compostura  = composure_base + tell_control − fatiga − tilt     (0..100)
///   fuga        = exceso × (100 − compostura) × (100 − control) / 1e6
/// ```
///
/// Las **microexpresiones** son fugas de duración muy corta: solo las ve quien mira en ese
/// instante (`attention_score`). La emisión de varios gestos se compone en una lista corta.
///
/// Todo entero, sin heap y determinista. Paramétrico vía `ExpressionParams`.
///
/// Verificación: HOST-200.

#include <eng/core/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/psyche_traits.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Canal por el que se expresa un gesto. Determina su visibilidad y su coste de control.
enum class ExpressionChannel : eng::u8 {
	FaceEyes = 0, ///< ojos y cejas
	FaceMouth,    ///< boca y nariz
	FaceBrow,     ///< frente y cejas
	Body,         ///< torso, brazos, manos
	Posture,      ///< postura global
	Gaze,         ///< dirección de la mirada
	Voice,        ///< voz y paraverbal
	Timing,       ///< ritmo de la apuesta/respuesta
	Count,
};

inline constexpr eng::usize channel_count = static_cast<eng::usize>(ExpressionChannel::Count);

/// Catálogo de gestos. `control` y `detect` se declaran por gesto en la tabla
/// `kGestures[]`; aquí solo la identidad.
enum class GestureKind : eng::u8 {
	// Ojos y cejas
	Blink = 0,
	BlinkFast,
	BlinkHold,
	PupilDilate,
	WideEyes,
	Squint,
	BrowRaise,
	BrowFurrow,
	ForeheadTension,
	EyeRoll,
	StareDown,
	GazeAversion,
	// Boca y nariz
	Smile,
	Smirk,
	Grimace,
	LipCornerTwitch,
	LipPress,
	LipBite,
	LickLips,
	Swallow,
	TongueOut,
	JawDrop,
	NoseFlare,
	NostrilTwitch,
	Yawn,
	// Cuerpo y postura
	ShoulderTension,
	Shrug,
	HeadTilt,
	HeadNod,
	HeadDown,
	NeckScratch,
	EarScratch,
	ArmScratch,
	HandTremor,
	FingerTap,
	Fidget,
	LegBounce,
	FootTap,
	LeanIn,
	LeanBack,
	CrossArms,
	OpenPosture,
	Slump,
	ChestPuff,
	FistClench,
	WipePalms,
	AdjustClothes,
	TouchFace,
	CoverMouth,
	SelfHug,
	// Voz
	Sigh,
	Cough,
	ThroatClear,
	Laugh,
	NervousLaugh,
	Whistle,
	Hum,
	BreathHold,
	SpeechRateFast,
	Silence,
	// Tiempo / apuesta
	Tank,
	InstantCall,
	InstantFold,
	InstantRaise,
	Overbet,
	MinBet,
	CheckDark,
	ChipFumble,
	BetRhythm,
	StackArrange,
	CountChips,
	Count,
};

inline constexpr eng::usize gesture_count = static_cast<eng::usize>(GestureKind::Count);

/// Propiedades de un gesto: canal, coste de control (0 = se escapa siempre, 100 = siempre
/// disimulable) y detectabilidad (cuánto cuesta verlo; 100 = evidente).
struct GestureDef {
	ExpressionChannel channel;
	eng::u8 control;
	eng::u8 detect;
};

/// Tabla de propiedades por gesto.
inline constexpr GestureDef kGestures[gesture_count] = {
	// Ojos y cejas
	{ExpressionChannel::FaceEyes, 10u, 20u},  // Blink
	{ExpressionChannel::FaceEyes, 20u, 60u},  // BlinkFast
	{ExpressionChannel::FaceEyes, 60u, 40u},  // BlinkHold
	{ExpressionChannel::FaceEyes, 5u, 50u},   // PupilDilate
	{ExpressionChannel::FaceEyes, 25u, 90u},  // WideEyes
	{ExpressionChannel::FaceEyes, 40u, 70u},  // Squint
	{ExpressionChannel::FaceBrow, 35u, 80u},  // BrowRaise
	{ExpressionChannel::FaceBrow, 30u, 75u},  // BrowFurrow
	{ExpressionChannel::FaceBrow, 20u, 55u},  // ForeheadTension
	{ExpressionChannel::FaceEyes, 70u, 90u},  // EyeRoll
	{ExpressionChannel::Gaze, 75u, 85u},      // StareDown
	{ExpressionChannel::Gaze, 45u, 60u},      // GazeAversion
	// Boca y nariz
	{ExpressionChannel::FaceMouth, 60u, 85u}, // Smile
	{ExpressionChannel::FaceMouth, 55u, 80u}, // Smirk
	{ExpressionChannel::FaceMouth, 25u, 80u}, // Grimace
	{ExpressionChannel::FaceMouth, 15u, 35u}, // LipCornerTwitch
	{ExpressionChannel::FaceMouth, 40u, 60u}, // LipPress
	{ExpressionChannel::FaceMouth, 30u, 55u}, // LipBite
	{ExpressionChannel::FaceMouth, 25u, 60u}, // LickLips
	{ExpressionChannel::FaceMouth, 20u, 45u}, // Swallow
	{ExpressionChannel::FaceMouth, 50u, 85u}, // TongueOut
	{ExpressionChannel::FaceMouth, 20u, 90u}, // JawDrop
	{ExpressionChannel::FaceMouth, 15u, 45u}, // NoseFlare
	{ExpressionChannel::FaceMouth, 15u, 40u}, // NostrilTwitch
	{ExpressionChannel::FaceMouth, 55u, 90u}, // Yawn
	// Cuerpo y postura
	{ExpressionChannel::Body, 30u, 55u},      // ShoulderTension
	{ExpressionChannel::Body, 70u, 80u},      // Shrug
	{ExpressionChannel::Body, 65u, 70u},      // HeadTilt
	{ExpressionChannel::Body, 70u, 80u},      // HeadNod
	{ExpressionChannel::Body, 45u, 70u},      // HeadDown
	{ExpressionChannel::Body, 50u, 60u},      // NeckScratch
	{ExpressionChannel::Body, 50u, 55u},      // EarScratch
	{ExpressionChannel::Body, 55u, 55u},      // ArmScratch
	{ExpressionChannel::Body, 10u, 50u},      // HandTremor
	{ExpressionChannel::Body, 35u, 60u},      // FingerTap
	{ExpressionChannel::Body, 30u, 50u},      // Fidget
	{ExpressionChannel::Body, 20u, 45u},      // LegBounce
	{ExpressionChannel::Body, 25u, 50u},      // FootTap
	{ExpressionChannel::Posture, 60u, 75u},   // LeanIn
	{ExpressionChannel::Posture, 60u, 75u},   // LeanBack
	{ExpressionChannel::Posture, 55u, 70u},   // CrossArms
	{ExpressionChannel::Posture, 65u, 70u},   // OpenPosture
	{ExpressionChannel::Posture, 35u, 65u},   // Slump
	{ExpressionChannel::Posture, 70u, 85u},   // ChestPuff
	{ExpressionChannel::Body, 25u, 50u},      // FistClench
	{ExpressionChannel::Body, 20u, 40u},      // WipePalms
	{ExpressionChannel::Body, 45u, 55u},      // AdjustClothes
	{ExpressionChannel::Body, 35u, 45u},      // TouchFace
	{ExpressionChannel::Body, 40u, 55u},      // CoverMouth
	{ExpressionChannel::Body, 30u, 50u},      // SelfHug
	// Voz
	{ExpressionChannel::Voice, 40u, 60u},     // Sigh
	{ExpressionChannel::Voice, 60u, 70u},     // Cough
	{ExpressionChannel::Voice, 55u, 65u},     // ThroatClear
	{ExpressionChannel::Voice, 60u, 85u},     // Laugh
	{ExpressionChannel::Voice, 30u, 60u},     // NervousLaugh
	{ExpressionChannel::Voice, 65u, 70u},     // Whistle
	{ExpressionChannel::Voice, 65u, 55u},     // Hum
	{ExpressionChannel::Voice, 25u, 40u},     // BreathHold
	{ExpressionChannel::Voice, 35u, 60u},     // SpeechRateFast
	{ExpressionChannel::Voice, 50u, 50u},     // Silence
	// Tiempo / apuesta
	{ExpressionChannel::Timing, 45u, 70u},    // Tank
	{ExpressionChannel::Timing, 35u, 75u},    // InstantCall
	{ExpressionChannel::Timing, 40u, 70u},    // InstantFold
	{ExpressionChannel::Timing, 30u, 75u},    // InstantRaise
	{ExpressionChannel::Timing, 50u, 80u},    // Overbet
	{ExpressionChannel::Timing, 55u, 65u},    // MinBet
	{ExpressionChannel::Timing, 60u, 70u},    // CheckDark
	{ExpressionChannel::Timing, 20u, 50u},    // ChipFumble
	{ExpressionChannel::Timing, 40u, 55u},    // BetRhythm
	{ExpressionChannel::Timing, 65u, 55u},    // StackArrange
	{ExpressionChannel::Timing, 60u, 50u},    // CountChips
};

[[nodiscard]] constexpr const GestureDef& gesture_def(GestureKind g) noexcept {
	return kGestures[static_cast<eng::usize>(g)];
}

/// Nombre legible de un gesto (diagnóstico).
[[nodiscard]] constexpr const char* gesture_name(GestureKind g) noexcept {
	switch (g) {
		case GestureKind::Blink: return "blink";
		case GestureKind::BlinkFast: return "blink_fast";
		case GestureKind::BlinkHold: return "blink_hold";
		case GestureKind::PupilDilate: return "pupil_dilate";
		case GestureKind::WideEyes: return "wide_eyes";
		case GestureKind::Squint: return "squint";
		case GestureKind::BrowRaise: return "brow_raise";
		case GestureKind::BrowFurrow: return "brow_furrow";
		case GestureKind::ForeheadTension: return "forehead_tension";
		case GestureKind::EyeRoll: return "eye_roll";
		case GestureKind::StareDown: return "stare_down";
		case GestureKind::GazeAversion: return "gaze_aversion";
		case GestureKind::Smile: return "smile";
		case GestureKind::Smirk: return "smirk";
		case GestureKind::Grimace: return "grimace";
		case GestureKind::LipCornerTwitch: return "lip_corner_twitch";
		case GestureKind::LipPress: return "lip_press";
		case GestureKind::LipBite: return "lip_bite";
		case GestureKind::LickLips: return "lick_lips";
		case GestureKind::Swallow: return "swallow";
		case GestureKind::TongueOut: return "tongue_out";
		case GestureKind::JawDrop: return "jaw_drop";
		case GestureKind::NoseFlare: return "nose_flare";
		case GestureKind::NostrilTwitch: return "nostril_twitch";
		case GestureKind::Yawn: return "yawn";
		case GestureKind::ShoulderTension: return "shoulder_tension";
		case GestureKind::Shrug: return "shrug";
		case GestureKind::HeadTilt: return "head_tilt";
		case GestureKind::HeadNod: return "head_nod";
		case GestureKind::HeadDown: return "head_down";
		case GestureKind::NeckScratch: return "neck_scratch";
		case GestureKind::EarScratch: return "ear_scratch";
		case GestureKind::ArmScratch: return "arm_scratch";
		case GestureKind::HandTremor: return "hand_tremor";
		case GestureKind::FingerTap: return "finger_tap";
		case GestureKind::Fidget: return "fidget";
		case GestureKind::LegBounce: return "leg_bounce";
		case GestureKind::FootTap: return "foot_tap";
		case GestureKind::LeanIn: return "lean_in";
		case GestureKind::LeanBack: return "lean_back";
		case GestureKind::CrossArms: return "cross_arms";
		case GestureKind::OpenPosture: return "open_posture";
		case GestureKind::Slump: return "slump";
		case GestureKind::ChestPuff: return "chest_puff";
		case GestureKind::FistClench: return "fist_clench";
		case GestureKind::WipePalms: return "wipe_palms";
		case GestureKind::AdjustClothes: return "adjust_clothes";
		case GestureKind::TouchFace: return "touch_face";
		case GestureKind::CoverMouth: return "cover_mouth";
		case GestureKind::SelfHug: return "self_hug";
		case GestureKind::Sigh: return "sigh";
		case GestureKind::Cough: return "cough";
		case GestureKind::ThroatClear: return "throat_clear";
		case GestureKind::Laugh: return "laugh";
		case GestureKind::NervousLaugh: return "nervous_laugh";
		case GestureKind::Whistle: return "whistle";
		case GestureKind::Hum: return "hum";
		case GestureKind::BreathHold: return "breath_hold";
		case GestureKind::SpeechRateFast: return "speech_rate_fast";
		case GestureKind::Silence: return "silence";
		case GestureKind::Tank: return "tank";
		case GestureKind::InstantCall: return "instant_call";
		case GestureKind::InstantFold: return "instant_fold";
		case GestureKind::InstantRaise: return "instant_raise";
		case GestureKind::Overbet: return "overbet";
		case GestureKind::MinBet: return "min_bet";
		case GestureKind::CheckDark: return "check_dark";
		case GestureKind::ChipFumble: return "chip_fumble";
		case GestureKind::BetRhythm: return "bet_rhythm";
		case GestureKind::StackArrange: return "stack_arrange";
		case GestureKind::CountChips: return "count_chips";
		default: return "?";
	}
}

/// Parámetros del cálculo de la fuga.
struct ExpressionParams {
	eng::u8 emotion_threshold = 40u;   ///< por debajo de esto no hay exceso (0..255)
	eng::u8 leak_visible = 25u;        ///< fuga mínima para que el gesto se vea
	eng::u8 micro_max = 60u;           ///< fuga por debajo de esto puede ser microexpresión
	eng::u8 fatigue_penalty = 60u;     ///< cuánto baja la fatiga (0..255) la compostura
	eng::u8 tilt_penalty = 60u;        ///< cuánto baja el tilt (0..255) la compostura
	eng::u8 anxiety_boost = 40u;       ///< cuánto sube la ansiedad (0..255) el exceso
};

/// Estado temporal que modula la compostura (lo llena `psyche.hpp`; aquí solo se usa).
struct LeakContext {
	eng::u8 composure_base = 50u; ///< compostura del carácter (0..100)
	eng::u8 tell_control = 0u;    ///< aptitud para ocultar (0..100)
	eng::u8 fatigue = 0u;         ///< cansancio (0..255)
	eng::u8 tilt = 0u;            ///< descontrol (0..255)
	eng::u8 anxiety = 0u;         ///< tensión (0..255)
};

/// Compostura efectiva en `[0,100]`: base + pericia − fatiga − tilt (saturada).
[[nodiscard]] constexpr eng::u8 effective_composure(const LeakContext& ctx,
                                                    const ExpressionParams& p) noexcept {
	eng::s32 c = static_cast<eng::s32>(ctx.composure_base) +
	             static_cast<eng::s32>(ctx.tell_control) / 2;
	c -= static_cast<eng::s32>(u8_scale(ctx.fatigue, p.fatigue_penalty)) / 2;
	c -= static_cast<eng::s32>(u8_scale(ctx.tilt, p.tilt_penalty)) / 2;
	if (c < 0) {
		return 0u;
	}
	if (c > 100) {
		return 100u;
	}
	return static_cast<eng::u8>(c);
}

/// Intensidad de fuga `[0,100]` de un gesto dado el exceso emocional y la compostura.
/// `exceso/255` × `(100−compostura)/100` × `(100−control)/100`, escalado a 100.
/// Todo producto es 16×16 (`mulu.w`), sin `__mulsi3`.
[[nodiscard]] constexpr eng::u8 leak_from(eng::u8 excess, eng::u8 composure,
                                          GestureKind gesture) noexcept {
	const GestureDef& def = gesture_def(gesture);
	// excess (0..255) * comp_factor (0..100) / 255 -> 0..100
	const eng::u8 a = static_cast<eng::u8>(
		eng::math::div_wide(eng::math::mul_wide(static_cast<eng::s16>(excess),
		                                        static_cast<eng::s16>(100u - composure)),
		                    static_cast<eng::s16>(255)));
	// a (0..100) * ctrl_factor (0..100) / 100 -> 0..100
	return static_cast<eng::u8>(
		eng::math::div_wide(eng::math::mul_wide(static_cast<eng::s16>(a),
		                                        static_cast<eng::s16>(100u - def.control)),
		                    static_cast<eng::s16>(100)));
}

/// Emoción que empuja cada gesto (aproximación: el afecto dominante de su canal).
[[nodiscard]] constexpr Emotion gesture_emotion(GestureKind g) noexcept {
	switch (g) {
		case GestureKind::BlinkFast:
		case GestureKind::PupilDilate:
		case GestureKind::WideEyes:
		case GestureKind::ForeheadTension:
		case GestureKind::Swallow:
		case GestureKind::NoseFlare:
		case GestureKind::HandTremor:
		case GestureKind::WipePalms:
		case GestureKind::BreathHold:
		case GestureKind::ChipFumble:
		case GestureKind::Tank:
			return Emotion::Fear;
		case GestureKind::BrowFurrow:
		case GestureKind::Grimace:
		case GestureKind::LipPress:
		case GestureKind::FistClench:
		case GestureKind::ShoulderTension:
		case GestureKind::StareDown:
		case GestureKind::Sigh:
			return Emotion::Anger;
		case GestureKind::Smile:
		case GestureKind::Laugh:
		case GestureKind::OpenPosture:
		case GestureKind::HeadNod:
			return Emotion::Joy;
		case GestureKind::HeadDown:
		case GestureKind::Slump:
			return Emotion::Sadness;
		case GestureKind::EyeRoll:
		case GestureKind::Smirk:
		case GestureKind::TongueOut:
		case GestureKind::NostrilTwitch:
			return Emotion::Hatred;
		default:
			return Emotion::Fear;
	}
}

/// Fuga de un gesto derivada del afecto dominante. El gesto declara qué emoción lo empuja
/// mediante `gesture_emotion`; aquí se combina con la compostura efectiva.
[[nodiscard]] constexpr eng::u8 leak(const Mind& mind, const LeakContext& ctx,
                                     GestureKind gesture,
                                     const ExpressionParams& p = ExpressionParams {}) noexcept {
	const Emotion e = gesture_emotion(gesture);
	eng::u8 affect = mind.emotions.at(e);
	// La ansiedad sube el exceso percibido (tensión general).
	affect = u8_sat_add(affect, u8_scale(ctx.anxiety, p.anxiety_boost));
	if (affect <= p.emotion_threshold) {
		return 0u;
	}
	const eng::u8 excess = static_cast<eng::u8>(affect - p.emotion_threshold);
	return leak_from(excess, effective_composure(ctx, p), gesture);
}

/// Un gesto activo con su intensidad de fuga.
struct LeakedGesture {
	GestureKind kind = GestureKind::Blink;
	eng::u8 intensity = 0u;
	bool micro = false; ///< true si es una microexpresión (dura muy poco)
};

/// Lista corta de gestos activos (los que se fugan por encima del umbral visible).
using LeakList = eng::util::StaticVector<LeakedGesture, 8u>;

/// Calcula los gestos que se fugan de una lista de candidatos (los relevantes del juego).
/// Los que superan `leak_visible` entran en `out`; los que quedan por debajo de
/// `micro_max` se marcan como microexpresión.
template <eng::usize N>
constexpr void compute_leaks(const Mind& mind, const LeakContext& ctx,
                             const GestureKind (&candidates)[N], LeakList& out,
                             const ExpressionParams& p = ExpressionParams {}) noexcept {
	out.clear();
	for (eng::usize i = 0u; i < N; ++i) {
		const eng::u8 intensity = leak(mind, ctx, candidates[i], p);
		if (intensity < p.leak_visible) {
			continue;
		}
		const bool micro = intensity <= p.micro_max;
		(void)out.push_back(LeakedGesture {candidates[i], intensity, micro});
	}
}

/// Entrada del **humano** como personaje: gestos explícitos (botones/combinaciones) y
/// gestos **implícitos** por *timing* (cuánto tarda en responder). El motor no distingue
/// el origen: produce las mismas `LeakedGesture` que un NPC, de modo que la mesa lo lee
/// igual (`read.hpp`).
struct InputExpression {
	bool gesture_a = false;       ///< gesto pactado A (p. ej. picor de oreja)
	bool gesture_b = false;       ///< gesto pactado B (p. ej. fosas nasales)
	bool gesture_c = false;       ///< gesto pactado C (p. ej. relamerse)
	bool quick_response = false;  ///< respondió de golpe (instant call/raise)
	bool long_tank = false;       ///< tardó mucho en responder (piensa/trampa)
	bool fidget = false;          ///< entrada errática (nervios)
};

/// Convierte la entrada del humano en fugas visibles. El *timing* es el canal más
/// natural: responder de golpe o tardar mucho son tells objetivos que no dependen de
/// dibujar una cara.
[[nodiscard]] constexpr LeakList expression_from_input(const InputExpression& in,
                                                       const ExpressionParams& p =
                                                           ExpressionParams {}) noexcept {
	LeakList out;
	const eng::u8 visible = p.leak_visible;
	auto add = [&](GestureKind g, eng::u8 intensity) {
		if (intensity >= visible) {
			(void)out.push_back(LeakedGesture {g, intensity, intensity <= p.micro_max});
		}
	};
	if (in.gesture_a) {
		add(GestureKind::EarScratch, 80u);
	}
	if (in.gesture_b) {
		add(GestureKind::NoseFlare, 80u);
	}
	if (in.gesture_c) {
		add(GestureKind::LickLips, 80u);
	}
	if (in.quick_response) {
		add(GestureKind::InstantCall, 70u);
	}
	if (in.long_tank) {
		add(GestureKind::Tank, 70u);
	}
	if (in.fidget) {
		add(GestureKind::Fidget, 60u);
	}
	return out;
}

} // namespace eng::sim
