#pragma once

#include <eosio/asset.hpp>

using std::string;

namespace eosio {

void from_string(asset& to, const string& from) {
   auto space_pos = from.find(' ');
   check(space_pos != string::npos, "asset needs numeric and symbol");

   auto numeric = from.substr(0, space_pos);
   auto dot_pos = numeric.find('.');

   uint8_t precision = (dot_pos == string::npos) ? 0 : numeric.size() - (dot_pos + 1);
   if (dot_pos != string::npos) {
      numeric.erase(dot_pos, 1);
   }

   to = {std::stoi(numeric), {from.substr(space_pos+1), precision}};
}

void from_string(extended_asset& to, const string& from) {
   auto at_pos = from.find('@');
   check(at_pos != string::npos, "extended_asset needs asset and contract name");

   from_string(to.quantity, from.substr(0, at_pos));
   to.contract = name(from.substr(at_pos+1));
}

}
