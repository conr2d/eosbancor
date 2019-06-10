#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <cmath>

using namespace eosio;
using std::string;

namespace eosio {

struct token {
   struct [[eosio::table]] currency_stats {
      asset    supply;
      asset    max_supply;
      name     issuer;

      uint64_t primary_key()const { return supply.symbol.code().raw(); }
   };

   typedef eosio::multi_index< "stat"_n, currency_stats > stats;

   asset get_supply( const symbol_code& sym_code ) {
      stats statstable( contract, sym_code.raw() );
      const auto& st = statstable.get( sym_code.raw() );
      return st.supply;
   }

   name get_issuer( const symbol_code& sym_code ) {
      stats statstable( contract, sym_code.raw() );
      const auto& st = statstable.get( sym_code.raw() );
      return st.issuer;
   }

   static constexpr name active_permission{"active"_n};

   token(name c, name auth = name()) {
      contract = c;
      if (auth != name())
         auths.emplace_back(permission_level(auth, active_permission));
   }

   void issue(name to, asset quantity, string memo = "") {
      if (auths.empty())
         auths.emplace_back(permission_level(get_issuer(quantity.symbol.code()), active_permission));
      action_wrapper<"issue"_n, &token::issue>(contract, auths).send(to, quantity, memo);
   }

   void retire(asset quantity, string memo = "") {
      if (auths.empty())
         auths.emplace_back(permission_level(get_issuer(quantity.symbol.code()), active_permission));
      action_wrapper<"retire"_n, &token::retire>(contract, auths).send(quantity, memo);
   }

   void transfer(name from, name to, asset quantity, string memo = "") {
      if (auths.empty())
         auths.emplace_back(permission_level(get_issuer(quantity.symbol.code()), active_permission));
      action_wrapper<"transfer"_n, &token::transfer>(contract, auths).send(from, to, quantity, memo);
   }

   name contract;
   std::vector<permission_level> auths;
};

struct connector {
   extended_symbol smart;
   asset balance;
   double weight = .5;
   bool activated = false;

   struct converted {
      extended_asset value;
      asset delta;
      double ratio;
   };

   converted convert_to_smart(const extended_asset& from, const extended_symbol& to, bool reverse = false) {
      const double S = token(to.get_contract()).get_supply(to.get_symbol().code()).amount; 
      const double C = balance.amount;
      const double dC = from.quantity.amount;

      double dS = S * (std::pow(1. + dC / C, weight) - 1.);
      if (dS < 0) dS = 0;

      auto conversion_rate = ((int64_t)dS) / dS;
      auto delta = asset{ from.quantity.amount - int64_t(from.quantity.amount * (1 - conversion_rate)), balance.symbol };

      if (!reverse) balance += delta;
      else balance -= delta;

      return { {int64_t(dS), to}, delta, conversion_rate };
   }

   converted convert_from_smart(const extended_asset& from, const extended_symbol& to, bool reverse = false) {
      const double C = balance.amount;
      const double S = token(from.contract).get_supply(from.quantity.symbol.code()).amount;
      const double dS = -from.quantity.amount;

      double dC = C * (std::pow(1. + dS / S, double(1) / weight) - 1.);
      if (dC > 0) dC = 0;

      auto delta = asset{ int64_t(-dC), balance.symbol };

      if (!reverse) balance -= delta;
      else balance += delta;

      return { {int64_t(-dC), to}, delta, ((int64_t)-dC) / (-dC) };
   }

   converted convert_to_exact_smart(const extended_symbol& from, const extended_asset& to) {
      return convert_from_smart(to, from, true);
   }

   converted convert_exact_from_smart(const extended_symbol& from, const extended_asset& to) {
      return convert_to_smart(to, from, true);
   }

   uint64_t primary_key() const { return smart.get_symbol().code().raw(); }

   EOSLIB_SERIALIZE(connector, (smart)(balance)(weight)(activated))
};

} /// namespace eosio
