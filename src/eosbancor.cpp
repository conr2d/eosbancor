#include <eosbancor/eosbancor.hpp>

void eosbancor::on_transfer(name from, name to, asset quantity, string memo) {
   if (from == _self)
      return;

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "contract not initialized");

   auto transferred = extended_asset(quantity, get_first_receiver());

   if (transferred.get_extended_symbol() == cfg.get().connected.get_extended_symbol()) {
      // initialize connector or buy smart
      auto at_pos = memo.find('@');
      check(at_pos != string::npos, "extended symbol code requires `@`");

      auto contract = name(memo.substr(at_pos+1));
      auto sym_code = symbol_code(memo.substr(0, at_pos));
      auto precision = token(contract).get_supply(sym_code).symbol.precision();

      connectors conn(_self, contract.value);
      auto it = conn.find(sym_code.raw());
      check(it != conn.end(), "connector not exists");

      conn.modify(it, same_payer, [&](auto& c) {
         if (c.activated) {
            charges chrg(_self, contract.value);
            auto cit = chrg.find(sym_code.raw());
            auto fee = cit != chrg.end() ? cit->get_fee(transferred)
                     : !cfg.get().is_exempted() ? cfg.get().get_fee(transferred)
                     : extended_asset(0, transferred.get_extended_symbol());

            auto quant_after_fee = transferred - fee;
            check(quant_after_fee.quantity.amount > 0, "paid token not enough after charging fee");

            auto smart_issued = c.convert_to_smart(quant_after_fee, {symbol(sym_code, precision), contract});
            check(smart_issued.value.quantity.amount > 0, "paid token not enough after charging fee");

            auto issuer = token(contract).get_issuer(sym_code);
            token(contract).issue(issuer, smart_issued.value.quantity);
            token(contract).transfer(issuer, from, smart_issued.value.quantity);

            auto refund = asset(int64_t(quant_after_fee.quantity.amount * (1 - smart_issued.ratio)), quant_after_fee.quantity.symbol);
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
            //print("effective_price = ", asset((quant_after_fee.quantity.amount - refund.amount) / smart_issued.value.quantity.amount * pow(10, smart_issued.value.quantity.symbol.precision()), quantity.symbol));
         } else {
            auto issuer = token(c.smart.get_contract()).get_issuer(c.smart.get_symbol().code());
            check(from == issuer, "issuer only can initialize connector");
            check(transferred == c.balance, "initial balance not match");
            c.activated = true;
         }
      });
   } else {
      // sell smart
      connectors conn(_self, transferred.contract.value);
      auto it = conn.find(transferred.quantity.symbol.code().raw());
      check(it != conn.end(), "connector not exists");

      conn.modify(it, same_payer, [&](auto& c) {
         check(c.activated, "connector not initialized");
         auto connected_out = c.convert_from_smart(transferred, cfg.get().connected.get_extended_symbol());

         charges chrg(_self, transferred.contract.value);
         auto cit = chrg.find(transferred.quantity.symbol.code().raw());
         auto fee = cit != chrg.end() ? cit->get_fee(connected_out.value)
                  : !cfg.get().is_exempted() ? cfg.get().get_fee(connected_out.value)
                  : extended_asset(0, connected_out.value.get_extended_symbol());

         auto quant_after_fee = connected_out.value - fee;
         check(quant_after_fee.quantity.amount > 0, "paid token not enough after charging fee");

         auto issuer = token(transferred.contract).get_issuer(transferred.quantity.symbol.code());
         auto refund = asset(int64_t(transferred.quantity.amount * (1 - connected_out.ratio)), transferred.quantity.symbol);
         token(transferred.contract, _self).transfer(_self, issuer, transferred.quantity - refund);
         token(transferred.contract).retire(transferred.quantity - refund);
         token(cfg.get().connected.contract, _self).transfer(_self, from, quant_after_fee.quantity);
         if (refund.amount > 0) {
            token(transferred.contract, _self).transfer(_self, from, refund, "refund not converted amount");
         }
         if (fee.quantity.amount > 0) {
            token(cfg.get().connected.contract, _self).transfer(_self, cfg.get().owner, fee.quantity, "conversion fee");
         }
         //print("effective_price = ", asset(connected_out.value.quantity.amount / (transferred.quantity.amount - refund.amount) * pow(10, transferred.quantity.symbol.precision()), connected_out.value.quantity.symbol));
      });
   }
}

void eosbancor::init(name owner, extended_symbol connected) {
   require_auth(_self);

   configuration cfg(_self, _self.value);
   check(!cfg.exists(), "already initialized");
   cfg.set({extended_asset{0, connected}, 0, owner}, _self);
}

void eosbancor::connect(extended_symbol smart, extended_asset balance, double weight) {
   auto issuer = token(smart.get_contract()).get_issuer(smart.get_symbol().code());
   require_auth(issuer);

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "contract not initialized");
   check(cfg.get().connected.get_extended_symbol() == balance.get_extended_symbol(), "balance should be paid by connected token");

   connectors conn(_self, smart.get_contract().value);
   auto it = conn.find(smart.get_symbol().code().raw());
   check(it == conn.end(), "existing connector");
   conn.emplace(issuer, [&](auto& c) {
      c.smart = smart;
      c.balance = balance;
      c.weight = weight;
   });
}

void eosbancor::setcharge(int16_t rate, std::optional<extended_asset> fixed, std::optional<extended_symbol> smart) {
   require_auth(_self);

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "initialize contract before setting charge");
   check(!fixed || cfg.get().connected.get_extended_symbol() == fixed->get_extended_symbol(), "represent conversion fee in connected token");

   if (!smart) {
      check(rate >= 0 && rate <= 1000, "rate needs to be in the range of 0-1000 (per mille)");
      auto it = cfg.get();
      it.rate = static_cast<uint16_t>(rate);
      if (fixed)
         it.connected = *fixed;
      cfg.set(it, _self);
   } else {
      charges chrg(_self, smart->get_contract().value);
      auto it = chrg.find(smart->get_symbol().code().raw());

      if (rate == -1) {
         check(it != chrg.end(), "no charge policy to be deleted");
         chrg.erase(it);
         return;
      }

      check(rate >= 0 && rate <= 1000, "rate needs to be in the range of 0-1000 (per mille)");
      if (it == chrg.end()) {
         chrg.emplace(_self, [&](auto& c) {
            c.smart = *smart;
            c.rate = static_cast<uint16_t>(rate);
            c.connected = fixed ? *fixed : extended_asset(0, cfg.get().connected.get_extended_symbol());
         });
      } else {
         chrg.modify(it, same_payer, [&](auto& c) {
            c.rate = static_cast<uint16_t>(rate);
            if (fixed)
               c.connected = *fixed;
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
