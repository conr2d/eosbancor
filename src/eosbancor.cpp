#include <eosbancor/eosbancor.hpp>
#include <eosbancor/types.hpp>
#include <eosbancor/dlog.hpp>

extended_asset eosbancor::get_fee(const extended_asset& value, const extended_asset& smart, const config& c, bool required) {
   extended_asset fee = { 0, value.get_extended_symbol() };
   bool exempted = true;

   charges chrg(_self, smart.contract.value);
   auto it = chrg.find(smart.quantity.symbol.code().raw());

   if (it != chrg.end()) {
      exempted = it->is_exempted();
      if (!exempted) {
         if (!required) fee = it->get_fee(value);
         else fee = it->get_required_fee(value);
      }
   } else {
      exempted = c.is_exempted();
      if (!exempted) {
         if (!required) fee = c.get_fee(value);
         else fee = c.get_required_fee(value);
      }
   }
   if (!exempted && fee.quantity.amount <= 0) {
      fee.quantity.amount = 1;
   }

   return fee;
}

void eosbancor::on_transfer(name from, name to, asset quantity, string memo) {
   if (from == _self)
      return;

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "contract not initialized");

   extended_asset target;
   auto space_pos = memo.find(' ');
   if (space_pos != string::npos) {
      from_string(target, memo);
      check(target.quantity.amount > 0, "specified amount should be positive");
   } else {
      auto at_pos = memo.find('@');
      check(at_pos != string::npos, "extended symbol code requires `@`");

      auto contract = name(memo.substr(at_pos+1));
      auto sym_code = symbol_code(memo.substr(0, at_pos));
      auto precision = token(contract).get_supply(sym_code).symbol.precision();

      target = {0, {{sym_code, precision}, contract}};
   }

   auto transferred = extended_asset(quantity, get_first_receiver());

   if (transferred.get_extended_symbol() == cfg.get().get_connected_symbol()) {
      // initialize connector or buy smart
      connectors conn(_self, target.contract.value);
      auto it = conn.find(target.quantity.symbol.code().raw());
      check(it != conn.end(), "connector not exists");

      conn.modify(it, same_payer, [&](auto& c) {
         if (c.activated) {
            if (target.quantity.amount == 0) {
               auto fee = get_fee(transferred, target, cfg.get());

               auto quant_after_fee = transferred - fee;
               check(quant_after_fee.quantity.amount > 0, "paid token not enough after charging fee");

               auto smart_issued = c.convert_to_smart(quant_after_fee, target.get_extended_symbol());
               check(smart_issued.value.quantity.amount > 0, "paid token not enough for conversion");

               auto issuer = token(target.contract).get_issuer(target.quantity.symbol.code());
               token(target.contract).issue(issuer, smart_issued.value.quantity);
               token(target.contract).transfer(issuer, from, smart_issued.value.quantity);

               auto refund = quant_after_fee.quantity - smart_issued.delta;
               if (refund.amount > 0) {
                  auto overcharged = int64_t(fee.quantity.amount * (1 - smart_issued.ratio));
                  if (overcharged < 0) overcharged = 0;
                  fee.quantity.amount -= overcharged;
                  refund.amount += overcharged;
                  token(transferred.contract, _self).transfer(_self, from, refund, "refund not converted amount");
               }
               if (fee.quantity.amount > 0) {
                  token(transferred.contract, _self).transfer(_self, cfg.get().owner, fee.quantity, "conversion fee");
               }
               dlog("conversion_rate = 0.", int64_t(smart_issued.ratio * 10000), ", ");
               dlog("effective_price = ", asset((quant_after_fee.quantity.amount - refund.amount) * pow(10, smart_issued.value.quantity.symbol.precision()) / smart_issued.value.quantity.amount, quantity.symbol));
            } else {
               auto connected_required = c.convert_to_exact_smart(transferred.get_extended_symbol(), target);
               auto fee = get_fee({connected_required.delta, connected_required.value.contract}, target, cfg.get(), true);

               auto refund = transferred.quantity - (connected_required.delta + fee.quantity);
               check(refund.amount >= 0, "paid token not enough to convert to specified amount");

               auto issuer = token(target.contract).get_issuer(target.quantity.symbol.code());
               token(target.contract).issue(issuer, target.quantity);
               token(target.contract).transfer(issuer, from, target.quantity);

               if (refund.amount > 0) {
                  token(transferred.contract, _self).transfer(_self, from, refund, "refund not converted amount");
               }
               if (fee.quantity.amount > 0) {
                  token(transferred.contract, _self).transfer(_self, cfg.get().owner, fee.quantity, "conversion fee");
               }
               dlog("conversion_rate = 0.", int64_t(connected_required.ratio * 10000), ", ");
               dlog("effective_price = ", asset(connected_required.delta.amount * pow(10, target.quantity.symbol.precision()) / target.quantity.amount, quantity.symbol));
            }
         } else {
            auto issuer = token(c.smart.get_contract()).get_issuer(c.smart.get_symbol().code());
            check(from == issuer, "issuer only can initialize connector");
            check(transferred.quantity == c.balance, "initial balance not match");
            c.activated = true;
         }
      });
   } else {
      // sell smart
      connectors conn(_self, transferred.contract.value);
      auto it = conn.find(transferred.quantity.symbol.code().raw());
      check(it != conn.end(), "connector not exists");

      conn.modify(it, same_payer, [&](auto& c) {
         if (target.quantity.amount == 0) {
            check(c.activated, "connector not initialized");

            auto connected_out = c.convert_from_smart(transferred, cfg.get().get_connected_symbol());
            auto fee = get_fee(connected_out.value, transferred, cfg.get());

            auto quant_after_fee = connected_out.value - fee;
            check(quant_after_fee.quantity.amount > 0, "paid token not enough after charging fee");

            auto issuer = token(transferred.contract).get_issuer(transferred.quantity.symbol.code());
            auto refund = asset(int64_t(transferred.quantity.amount * (1 - connected_out.ratio)), transferred.quantity.symbol);
            token(transferred.contract, _self).transfer(_self, issuer, transferred.quantity - refund);
            token(transferred.contract).retire(transferred.quantity - refund);
            token(cfg.get().connected_contract, _self).transfer(_self, from, quant_after_fee.quantity);
            if (refund.amount > 0) {
               token(transferred.contract, _self).transfer(_self, from, refund, "refund not converted amount");
            }
            if (fee.quantity.amount > 0) {
               token(cfg.get().connected_contract, _self).transfer(_self, cfg.get().owner, fee.quantity, "conversion fee");
            }
            dlog("conversion_rate = 0.", int64_t(connected_out.ratio * 10000), ", ");
            dlog("effective_price = ", asset(connected_out.delta.amount * pow(10, transferred.quantity.symbol.precision()) / (transferred.quantity.amount - refund.amount), connected_out.value.quantity.symbol));
         } else {
            auto fee = get_fee(target, transferred, cfg.get(), true);

            auto smart_required = c.convert_exact_from_smart(transferred.get_extended_symbol(), target + fee);
            target.quantity = smart_required.delta - fee.quantity;

            auto refund = transferred.quantity - smart_required.value.quantity;
            check(refund.amount >= 0, "paid token not enough to convert to specified amount");

            auto issuer = token(transferred.contract).get_issuer(transferred.quantity.symbol.code());
            token(transferred.contract, _self).transfer(_self, issuer, transferred.quantity - refund);
            token(transferred.contract).retire(transferred.quantity - refund);
            token(target.contract, _self).transfer(_self, from, target.quantity);
            if (refund.amount > 0) {
               token(transferred.contract, _self).transfer(_self, from, refund, "refund not converted amount");
            }
            if (fee.quantity.amount > 0) {
               token(target.contract, _self).transfer(_self, cfg.get().owner, fee.quantity, "conversion fee");
            }
            dlog("conversion_rate = 0.", int64_t(smart_required.ratio * 10000), ", ");
            dlog("effective_price = ", asset(smart_required.delta.amount * pow(10, transferred.quantity.symbol.precision()) / (transferred.quantity.amount - refund.amount), target.quantity.symbol));
         }
      });
   }
}

void eosbancor::init(name owner, extended_symbol connected) {
   require_auth(_self);

   configuration cfg(_self, _self.value);
   check(!cfg.exists(), "already initialized");
   cfg.set({0, {0, connected.get_symbol()}, connected.get_contract(), owner}, _self);
}

void eosbancor::connect(extended_symbol smart, extended_asset balance, double weight) {
   auto issuer = token(smart.get_contract()).get_issuer(smart.get_symbol().code());
   require_auth(issuer);

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "contract not initialized");
   check(cfg.get().get_connected_symbol() == balance.get_extended_symbol(), "balance should be paid by connected token");

   connectors conn(_self, smart.get_contract().value);
   auto it = conn.find(smart.get_symbol().code().raw());
   check(it == conn.end(), "existing connector");
   conn.emplace(issuer, [&](auto& c) {
      c.smart = smart;
      c.balance = balance.quantity;
      c.weight = weight;
   });
}

void eosbancor::setcharge(int16_t rate, std::optional<extended_asset> fixed, std::optional<extended_symbol> smart) {
   require_auth(_self);

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "initialize contract before setting charge");
   check(!fixed || cfg.get().get_connected_symbol() == fixed->get_extended_symbol(), "represent conversion fee in connected token");

   if (!smart) {
      check(rate >= 0 && rate <= 10000, "rate needs to be in the range of 0-10000 (permyriad)");
      auto it = cfg.get();
      it.rate = static_cast<uint16_t>(rate);
      if (fixed)
         it.connected = fixed->quantity;
      cfg.set(it, _self);
   } else {
      charges chrg(_self, smart->get_contract().value);
      auto it = chrg.find(smart->get_symbol().code().raw());

      if (rate == -1) {
         check(it != chrg.end(), "no charge policy to be deleted");
         chrg.erase(it);
         return;
      }

      check(rate >= 0 && rate <= 10000, "rate needs to be in the range of 0-10000 (permyriad)");
      if (it == chrg.end()) {
         chrg.emplace(_self, [&](auto& c) {
            c.smart = *smart;
            c.rate = static_cast<uint16_t>(rate);
            c.connected = fixed ? fixed->quantity : asset{0, cfg.get().connected.symbol};
         });
      } else {
         chrg.modify(it, same_payer, [&](auto& c) {
            c.rate = static_cast<uint16_t>(rate);
            if (fixed)
               c.connected = fixed->quantity;
         });
      }
   }
}

void eosbancor::setowner(name owner) {
   require_auth(_self);

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "initialize contract before setting owner");

   auto it = cfg.get();
   it.owner = owner;
   cfg.set(it, _self);
}
