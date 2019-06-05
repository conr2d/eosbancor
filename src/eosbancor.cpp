#include <eosbancor/eosbancor.hpp>

void eosbancor::on_transfer(name from, name to, asset quantity, string memo) {
   if (from == _self)
      return;

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "contract not initialized");

   auto transferred = extended_asset(quantity, get_first_receiver());

   if (transferred.get_extended_symbol() == cfg.get().connected) {
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
            auto smart_issued = c.convert_to_smart(transferred, {symbol(sym_code, precision), contract});
            auto issuer = token(contract).get_issuer(sym_code);
            token(contract).issue(issuer, smart_issued.value.quantity);
            token(contract).transfer(issuer, from, smart_issued.value.quantity);
            auto refund = asset(int64_t(transferred.quantity.amount * (1 - smart_issued.ratio)), transferred.quantity.symbol);
            if (refund.amount > 0)
               token(transferred.contract, _self).transfer(_self, from, refund, "refund not converted amount");
            //print("effective_price = ", asset((transferred.quantity.amount - refund.amount) / smart_issued.value.quantity.amount * pow(10, smart_issued.value.quantity.symbol.precision()), quantity.symbol));
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

         auto connected_paidout = c.convert_from_smart(transferred, cfg.get().connected);
         auto issuer = token(transferred.contract).get_issuer(transferred.quantity.symbol.code());
         auto refund = asset(int64_t(transferred.quantity.amount * (1 - connected_paidout.ratio)), transferred.quantity.symbol);
         token(transferred.contract, _self).transfer(_self, issuer, transferred.quantity - refund);
         token(transferred.contract).retire(transferred.quantity - refund);
         token(cfg.get().connected.get_contract(), _self).transfer(_self, from, connected_paidout.value.quantity);
         if (refund.amount > 0)
            token(transferred.contract, _self).transfer(_self, from, refund, "refund not converted amount");
         //print("effective_price = ", asset(connected_paidout.value.quantity.amount / (transferred.quantity.amount - refund.amount) * pow(10, transferred.quantity.symbol.precision()), connected_paidout.value.quantity.symbol));
      });
   }
}

void eosbancor::init(extended_symbol connected) {
   require_auth(_self);

   configuration cfg(_self, _self.value);
   check(!cfg.exists(), "already initialized");
   cfg.set({connected}, _self);
}

void eosbancor::connect(extended_symbol smart, extended_asset balance, double weight) {
   auto issuer = token(smart.get_contract()).get_issuer(smart.get_symbol().code());
   require_auth(issuer);

   configuration cfg(_self, _self.value);
   check(cfg.exists(), "contract not initialized");
   check(cfg.get().connected == balance.get_extended_symbol(), "balance should be paid by connected token");

   connectors conn(_self, smart.get_contract().value);
   auto it = conn.find(smart.get_symbol().code().raw());
   check(it == conn.end(), "existing connector");
   conn.emplace(issuer, [&](auto& c) {
      c.smart = smart;
      c.balance = balance;
      c.weight = weight;
   });
}
