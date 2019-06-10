#pragma once

#include <eosio/print.hpp>

template<typename... Args>
void dlog(Args&&... args) {

#ifdef NDEBUG
   static constexpr bool debug = false;
#else
   static constexpr bool debug = true;
#endif

   if (debug)
      eosio::print(std::forward<Args>(args)...);
}


