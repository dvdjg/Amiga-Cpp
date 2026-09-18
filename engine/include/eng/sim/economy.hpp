#pragma once

/// \file economy.hpp
/// **Economía y reputación** (`eng::sim`): precios que responden a la oferta y la demanda,
/// y **regalos/tributos** que suben la reputación de una facción. Es el motor de las
/// relaciones entre grupos: quien trae comida o materiales valiosos mejora su lugar; quien
/// necesita algo paga más.
///
/// - `Economy`: precio por tipo de objeto (derivado de `base_price` + demanda) y demanda
///   acumulada. `register_demand` sube el precio; `register_supply` lo baja; `tick_decay`
///   lo devuelve hacia la base.
/// - `gift_reputation`: cuánta reputación da un regalo según su valor.
/// - `give_gift`/`offer_tribute`: aplican el efecto sobre `Society` y la economía.
///
/// Todo entero y paramétrico (`EconomyParams`); se apoya en `Society` (reputación) y en
/// `Inventory` (tipos de objeto), sin duplicarlos.
///
/// Verificación: HOST-157.

#include <eng/ai/decision/utility.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/inventory.hpp>
#include <eng/sim/society.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Parámetros económicos.
struct EconomyParams {
	eng::s16 base_price[item_kinds] {5, 3, 8, 20, 50};
	eng::u8 demand_decay = 2u;     ///< cuánta demanda se pierde por tick
	eng::s16 demand_price = 1;     ///< subida de precio por unidad de demanda
	eng::s16 gift_rep = 4;         ///< reputación por unidad regalada
	eng::s16 tribute_rep = 8;      ///< reputación por unidad de tributo
};

/// Mercado por tipos de objeto.
struct Economy {
	eng::s16 price[item_kinds] {};
	eng::u8 demand[item_kinds] {};

	constexpr void reset(const EconomyParams& p = EconomyParams {}) noexcept {
		for (eng::usize i = 0; i < item_kinds; ++i) {
			price[i] = p.base_price[i];
			demand[i] = 0u;
		}
	}

	[[nodiscard]] constexpr eng::s16 value(ItemKind k) const noexcept {
		const eng::usize i = static_cast<eng::usize>(k);
		return i < item_kinds ? price[i] : 0;
	}

	[[nodiscard]] constexpr eng::u8 demand_of(ItemKind k) const noexcept {
		const eng::usize i = static_cast<eng::usize>(k);
		return i < item_kinds ? demand[i] : 0u;
	}

	/// Sube la demanda (y el precio) de un objeto.
	constexpr void register_demand(ItemKind k, eng::u8 amount,
				       const EconomyParams& p = EconomyParams {}) noexcept {
		const eng::usize i = static_cast<eng::usize>(k);
		if (i >= item_kinds) {
			return;
		}
		demand[i] = u8_sat_add(demand[i], amount);
		price[i] = static_cast<eng::s16>(p.base_price[i] + demand[i] * p.demand_price);
	}

	/// La oferta abarata el objeto (nunca por debajo de 1).
	constexpr void register_supply(ItemKind k, eng::u8 amount) noexcept {
		const eng::usize i = static_cast<eng::usize>(k);
		if (i >= item_kinds) {
			return;
		}
		const eng::s16 next = static_cast<eng::s16>(price[i] - amount);
		price[i] = next < 1 ? static_cast<eng::s16>(1) : next;
	}

	/// Envejece la demanda y recalcula precios hacia la base.
	constexpr void tick_decay(const EconomyParams& p = EconomyParams {}) noexcept {
		for (eng::usize i = 0; i < item_kinds; ++i) {
			demand[i] = u8_sat_sub(demand[i], p.demand_decay);
			price[i] = static_cast<eng::s16>(p.base_price[i] + demand[i] * p.demand_price);
		}
	}
};

/// Reputación que otorga regalar `amount` de un objeto (según su valor de mercado).
[[nodiscard]] constexpr eng::s16 gift_reputation(const Economy& eco, ItemKind k,
						 eng::u8 amount,
						 const EconomyParams& p = EconomyParams {}) noexcept {
	const eng::s16 base = eco.value(k);
	return static_cast<eng::s16>(p.gift_rep * amount + base / 2);
}

/// Aplica un regalo a la reputación de `receiver` y sube la demanda del objeto.
[[nodiscard]] constexpr eng::s16 give_gift(Society& society, Economy& eco, FactionId receiver,
					   ItemKind k, eng::u8 amount,
					   const EconomyParams& p = EconomyParams {}) noexcept {
	const eng::s16 gain = gift_reputation(eco, k, amount, p);
	society.adjust(receiver, gain);
	eco.register_demand(k, amount, p);
	return gain;
}

/// Tributo: un regalo formal con más peso reputacional.
[[nodiscard]] constexpr eng::s16 offer_tribute(Society& society, Economy& eco,
					       FactionId receiver, ItemKind k, eng::u8 amount,
					       const EconomyParams& p = EconomyParams {}) noexcept {
	const eng::s16 gain = static_cast<eng::s16>((p.tribute_rep - p.gift_rep) * amount +
						    gift_reputation(eco, k, amount, p));
	society.adjust(receiver, gain > 0 ? gain : gift_reputation(eco, k, amount, p));
	eco.register_demand(k, amount, p);
	return gain;
}

/// Un intercambio: `give` (lo que uno entrega) por `want` (lo que recibe).
struct TradeOffer {
	ItemKind give = ItemKind::Food;
	eng::u8 give_amount = 1u;
	ItemKind want = ItemKind::Material;
	eng::u8 want_amount = 1u;
};

/// Parámetros del regateo.
struct TradeParams {
	eng::u8 value_weight = 100u; ///< peso del balance de valor
	eng::u8 need_weight = 60u;   ///< peso de la necesidad de lo que se recibe
	eng::u16 accept_threshold = 400u; ///< puntuación mínima para aceptar
	eng::s16 rep_gain = 4;       ///< reputación que gana cada parte al comerciar
};

/// Balance de valor del intercambio: positivo = conviene (recibes más de lo que das).
[[nodiscard]] constexpr eng::s32 trade_value(const Economy& eco,
					     const TradeOffer& o) noexcept {
	return static_cast<eng::s32>(eco.value(o.want)) * o.want_amount -
	       static_cast<eng::s32>(eco.value(o.give)) * o.give_amount;
}

/// Puntuación de regateo `[0,1000]`: cuánto conviene aceptar el trato. Sube con el
/// balance de valor y con la **necesidad** de lo que se recibe (`need_for_want` 0..255).
[[nodiscard]] constexpr Score bargain_score(const Economy& eco, const TradeOffer& o,
					    eng::u8 need_for_want,
					    const TradeParams& p = TradeParams {}) noexcept {
	eng::ai::Utility u;
	u.add(static_cast<eng::s32>(500) + trade_value(eco, o), p.value_weight);
	u.add(static_cast<eng::s32>(need_for_want) * 4, p.need_weight);
	return static_cast<Score>(u.score());
}

/// Ejecuta el intercambio material: `giver` entrega `give` y recibe `want`; `taker` al
/// revés. Requiere que ambos tengan lo suyo y sube la demanda de lo intercambiado.
constexpr bool execute_trade(Inventory& giver, Inventory& taker, Economy& eco,
			     const TradeOffer& o,
			     const TradeParams& p = TradeParams {}) noexcept {
	(void)p;
	if (!giver.has(o.give, o.give_amount) || !taker.has(o.want, o.want_amount)) {
		return false;
	}
	(void)giver.remove(o.give, o.give_amount);
	(void)taker.add(o.give, o.give_amount);
	(void)taker.remove(o.want, o.want_amount);
	(void)giver.add(o.want, o.want_amount);
	eco.register_demand(o.want, o.want_amount);
	eco.register_demand(o.give, o.give_amount);
	return true;
}

} // namespace eng::sim
