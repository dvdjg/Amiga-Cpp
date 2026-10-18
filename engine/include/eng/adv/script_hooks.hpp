#pragma once

/// \file script_hooks.hpp
/// **Contrato del mundo de guion** para el código generado por `translate/emit.mjs` (I1):
/// las funciones de guion son plantillas `template <class W> void scrp_...(W& w)` y usan solo
/// este vocabulario (variables, bits, primitivas de aventura). El juego/engine implementa `W`
/// (p. ej. sobre `eng::adv` + `eng::rpg`); `NullScriptWorld` es la implementación de
/// referencia sin efectos, útil para compilar/validar el código generado en host.
///
/// Reglas del contrato: `var/local/bit` devuelven referencias `s32` (lectura/escritura);
/// las primitivas devuelven `s32` (0 = ok/falso) para poder usarse en expresiones; los
/// argumentos de **lista** del original (`startScript`, `setClass`, `setVarRange`…) llegan
/// como `w.list(…)` (`arg_list`, por valor); `ActorOps`/`VerbOps` llegan como **una llamada
/// por sub-operación** (`w.actor_op`/`w.verb_op` con los enums de `types.hpp`); las no
/// soportadas por el traductor llegan como `unsupported`/`unsupported_value` y quedan
/// contabilizadas en el informe de traducción.

#include <eng/adv/types.hpp>
#include <eng/core/types/types.hpp>

namespace eng::adv {

/// Implementación **nula** del mundo de guion: estado local propio y primitivas no-op.
/// Sirve para pruebas de compilación/ejecución del código generado y como esqueleto del
/// mundo real (que delegará en la escena, el kernel RPG y el audio).
struct NullScriptWorld {
	eng::s32 vars[256] {};   ///< variables globales (VAR_*)
	eng::s32 locals[32] {};  ///< locales del guion
	eng::s32 bits[2048] {};  ///< bits globales (flags)
	eng::s32 unsupported_calls {0}; ///< contador de primitivas no traducidas (telemetría)

	/// Variable global por índice (`Var[n]`).
	/// \param i índice (fuera de rango → 0).
	/// \return referencia al valor.
	eng::s32& var(int i) { return vars[(i >= 0 && i < 256) ? i : 0]; }
	/// Variable global por nombre simbólico (`VAR_*` de descumm).
	/// \param name nombre del original (se resuelve en el mundo real; aquí cae en 0).
	/// \return referencia al valor.
	eng::s32& var_name(const char* name) { (void)name; return vars[0]; }
	/// Variable local del guion (`Local[n]`).
	/// \param i índice (fuera de rango → 0).
	/// \return referencia al valor.
	eng::s32& local(int i) { return locals[(i >= 0 && i < 32) ? i : 0]; }
	/// Bit global (`Bit[n]`) como `s32` (0/1).
	/// \param i índice (fuera de rango → 0).
	/// \return referencia al valor.
	eng::s32& bit(int i) { return bits[(i >= 0 && i < 2048) ? i : 0]; }

	/// Lista de valores de un argumento de array del original (`w.list(3, 5)` en el guion
	/// generado): por valor, para que viva en la pila del guion (sin asignaciones dinámicas).
	/// \param a valores (siempre `s32`); el mundo real los interpreta según la primitiva.
	/// \return `arg_list` con los valores en orden.
	template <class... A>
	arg_list<sizeof...(A)> list(A... a) {
		return arg_list<sizeof...(A)> {static_cast<eng::s32>(a)...};
	}

	/// Primitiva no traducida usada como sentencia.
	/// \param name nombre de la primitiva original.
	void unsupported(const char* name) { (void)name; unsupported_calls++; }
	/// Primitiva no traducida usada en una expresión.
	/// \param name nombre de la primitiva original.
	/// \return 0 (sin efecto).
	eng::s32 unsupported_value(const char* name) { (void)name; unsupported_calls++; return 0; }
	/// Constante simbólica del original (`STOP`, `F`…): el mundo real mapea el nombre.
	/// \param name nombre simbólico.
	/// \return valor de la constante (0 en el mundo nulo).
	eng::s32 const_name(const char* name) { (void)name; return 0; }

	// --- primitivas de aventura (aridad segun el original: variadicas en el mundo nulo) ---
	template <class... A> void say(eng::s32, A...) {}
	template <class... A> void say_ego(A...) {}
	template <class... A> eng::s32 walk_to(A...) { return 0; }
	template <class... A> eng::s32 walk_to_actor(A...) { return 0; }
	template <class... A> eng::s32 walk_to_object(A...) { return 0; }
	template <class... A> eng::s32 put_actor(A...) { return 0; }
	template <class... A> eng::s32 put_actor_in_room(A...) { return 0; }
	template <class... A> eng::s32 put_actor_at_object(A...) { return 0; }
	template <class... A> eng::s32 animate(A...) { return 0; }
	template <class... A> eng::s32 face_actor(A...) { return 0; }
	template <class... A> eng::s32 start_script(A...) { return 0; }
	template <class... A> eng::s32 stop_script(A...) { return 0; }
	template <class... A> eng::s32 change_room(A...) { return 0; }
	template <class... A> eng::s32 change_room_with_ego(A...) { return 0; }
	template <class... A> eng::s32 set_state(A...) { return 0; }
	template <class... A> eng::s32 get_object_state(A...) { return 0; }
	template <class... A> eng::s32 get_object_owner(A...) { return 0; }
	template <class... A> eng::s32 set_owner(A...) { return 0; }
	template <class... A> eng::s32 pick_up(A...) { return 0; }
	template <class... A> eng::s32 set_class(A...) { return 0; }
	template <class... A> eng::s32 has_class(A...) { return 0; }
	template <class... A> eng::s32 start_sound(A...) { return 0; }
	template <class... A> eng::s32 stop_sound(A...) { return 0; }
	template <class... A> eng::s32 start_music(A...) { return 0; }
	template <class... A> eng::s32 stop_music(A...) { return 0; }
	template <class... A> eng::s32 sound_kludge(A...) { return 0; }
	template <class... A> eng::s32 delay(A...) { return 0; }
	template <class... A> eng::s32 delay_var(A...) { return 0; }
	template <class... A> eng::s32 pan_camera(A...) { return 0; }
	template <class... A> eng::s32 set_camera(A...) { return 0; }
	template <class... A> eng::s32 draw_object(A...) { return 0; }
	template <class... A> eng::s32 cursor(A...) { return 0; }
	template <class... A> eng::s32 cursor_hide(A...) { return 0; }
	template <class... A> eng::s32 userput(A...) { return 0; }
	template <class... A> eng::s32 random(A...) { return 0; }
	template <class... A> eng::s32 actor_room(A...) { return 0; }
	template <class... A> eng::s32 actor_x(A...) { return 0; }
	template <class... A> eng::s32 actor_y(A...) { return 0; }
	template <class... A> eng::s32 actor_moving(A...) { return 0; }
	template <class... A> eng::s32 actor_width(A...) { return 0; }
	template <class... A> eng::s32 verb_entrypoint(A...) { return 0; }
	template <class... A> eng::s32 inventory_count(A...) { return 0; }
	template <class... A> eng::s32 script_running(A...) { return 0; }
	template <class... A> eng::s32 begin_override(A...) { return 0; }
	template <class... A> eng::s32 end_override(A...) { return 0; }
	template <class... A> eng::s32 stop(A...) { return 0; }
	template <class... A> eng::s32 break_here(A...) { return 0; }
	template <class... A> eng::s32 wait_message(A...) { return 0; }
	template <class... A> eng::s32 wait_actor(A...) { return 0; }
	template <class... A> eng::s32 cutscene(A...) { return 0; }
	template <class... A> eng::s32 end_cutscene(A...) { return 0; }
	template <class... A> eng::s32 set_box_flags(A...) { return 0; }
	template <class... A> eng::s32 set_string_char(A...) { return 0; }
	template <class... A> eng::s32 load_resource(A...) { return 0; }
	template <class... A> eng::s32 lock_resource(A...) { return 0; }
	/// Una llamada por sub-operación de `ActorOps` (`w.actor_op(actor, eng::adv::ActorOp::x, …)`).
	template <class... A> eng::s32 actor_op(eng::s32, ActorOp, A...) { return 0; }
	/// Una llamada por sub-operación de `VerbOps` (`w.verb_op(verbo, eng::adv::VerbOp::x, …)`).
	template <class... A> eng::s32 verb_op(eng::s32, VerbOp, A...) { return 0; }
	template <class... A> eng::s32 do_sentence(A...) { return 0; }
	template <class... A> eng::s32 init_charset(A...) { return 0; }
	template <class... A> eng::s32 set_object_name(A...) { return 0; }
	template <class... A> eng::s32 cursor_show(A...) { return 0; }
	template <class... A> eng::s32 cursor_command(A...) { return 0; }
	template <class... A> eng::s32 set_pal_color(A...) { return 0; }
	template <class... A> eng::s32 chain_script(A...) { return 0; }
	template <class... A> eng::s32 actor_follow_camera(A...) { return 0; }
	template <class... A> eng::s32 wait_camera(A...) { return 0; }
	template <class... A> eng::s32 screen_effect(A...) { return 0; }
	template <class... A> eng::s32 start_object(A...) { return 0; }
	template <class... A> eng::s32 stop_object(A...) { return 0; }
	template <class... A> eng::s32 set_screen(A...) { return 0; }
	template <class... A> eng::s32 set_rgb_room_intensity(A...) { return 0; }
	template <class... A> eng::s32 room_intensity(A...) { return 0; }
	template <class... A> eng::s32 draw_box(A...) { return 0; }
	template <class... A> eng::s32 shake_on(A...) { return 0; }
	template <class... A> eng::s32 shake_off(A...) { return 0; }
	template <class... A> eng::s32 save_verbs(A...) { return 0; }
	template <class... A> eng::s32 restore_verbs(A...) { return 0; }
	template <class... A> eng::s32 put_code_in_string(A...) { return 0; }
	template <class... A> eng::s32 set_var_range(A...) { return 0; }
	template <class... A> eng::s32 load_costume(A...) { return 0; }
	template <class... A> eng::s32 lock_costume(A...) { return 0; }
	template <class... A> eng::s32 unlock_costume(A...) { return 0; }
	template <class... A> eng::s32 load_sound(A...) { return 0; }
	template <class... A> eng::s32 lock_sound(A...) { return 0; }
	template <class... A> eng::s32 unlock_sound(A...) { return 0; }
	template <class... A> eng::s32 unlock_script(A...) { return 0; }
	template <class... A> eng::s32 load_fl_object(A...) { return 0; }
};

} // namespace eng::adv
