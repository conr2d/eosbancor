#pragma once

#include <eosio/print.hpp>

namespace eosio {

template<typename... Args>
inline void dlog(Args&&... args) {
#ifndef NDEBUG
   eosio::print(std::forward<Args>(args)...);
#endif
}

} /// namespace eosio
