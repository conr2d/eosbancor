#include <eosio/singleton.hpp>
#include "connector.hpp"

class [[eosio::contract]] eosbancor : public contract {
public:
   using contract::contract;

   struct [[eosio::table]] connector : eosio::connector {
   };

   struct base_config {
      uint16_t rate; // permyriad
      asset connected; // if amount != 0, it's considered as fixed amount of conversion fee

      bool is_exempted()const { return connected.amount == 0 && rate == 0; }

      extended_asset get_fee(extended_asset value)const {
         int64_t fee = 0;
         if (rate != 0) {
            int64_t p = 10000 / rate;
            fee = (value.quantity.amount + p - 1) / p;
         }
         fee += connected.amount;
         return {fee, value.get_extended_symbol()};
      }

      extended_asset get_required_fee(extended_asset value)const {
         int64_t fee = 0;
         if (rate != 0) {
            int64_t p = 10000 / rate;
            fee = int64_t((value.quantity.amount + connected.amount) * p / double(p - 1) + 0.9) - value.quantity.amount;
         }
         fee += connected.amount;
         return {fee, value.get_extended_symbol()};
      }

      EOSLIB_SERIALIZE(base_config, (rate)(connected))
   };

   struct [[eosio::table]] charge_policy : base_config {
      extended_symbol smart;

      uint64_t primary_key() const { return smart.get_symbol().code().raw(); }

      EOSLIB_SERIALIZE_DERIVED(charge_policy, base_config, (smart))
   };

   struct [[eosio::table]] config : base_config {
      name connected_contract;
      name owner;

      extended_asset get_connected()const { return { connected, connected_contract }; }
      extended_symbol get_connected_symbol()const { return { connected.symbol, connected_contract }; }

      EOSLIB_SERIALIZE_DERIVED(config, base_config, (connected_contract)(owner))
   };

   typedef multi_index<"connector"_n, connector> connectors;
   typedef multi_index<"charge"_n, charge_policy> charges;
   typedef singleton<"config"_n, config> configuration;

   [[eosio::on_notify("eosio.token::transfer")]]
   void on_eos_transfer(name from, name to, asset quantity, string memo) {
      on_transfer(from, to, quantity, memo);
   }

   [[eosio::on_notify("*::transfer")]]
   void on_transfer(name from, name to, asset quantity, string memo);

   [[eosio::action]]
   void init(name owner, extended_symbol connected);

   [[eosio::action]]
   void connect(extended_symbol smart, extended_asset balance, double weight);

   [[eosio::action]]
   void setcharge(int16_t rate, std::optional<extended_asset> fixed, std::optional<extended_symbol> smart);

   [[eosio::action]]
   void setowner(name owner);

private:
   extended_asset get_fee(const extended_asset& value, const extended_asset& smart, const config& c, bool required = false);
};
