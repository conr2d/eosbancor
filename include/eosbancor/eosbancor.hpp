#include <eosio/singleton.hpp>
#include "connector.hpp"

class [[eosio::contract]] eosbancor : public contract {
public:
   using contract::contract;

   struct [[eosio::table]] connector : eosio::connector {
   };

   struct [[eosio::table]] config {
      extended_symbol connected;

      EOSLIB_SERIALIZE(config, (connected))
   };

   typedef multi_index<"connector"_n, connector> connectors;
   typedef singleton<"config"_n, config> configuration;

   [[eosio::on_notify("eosio.token::transfer")]]
   void on_eos_transfer(name from, name to, asset quantity, string memo) {
      on_transfer(from, to, quantity, memo);
   }

   [[eosio::on_notify("*::transfer")]]
   void on_transfer(name from, name to, asset quantity, string memo);

   [[eosio::action]]
   void init(extended_symbol connected);

   [[eosio::action]]
   void connect(extended_symbol smart, extended_asset balance, double weight);
};
