# EOS Bancor Contract

This contract implements bancor contract which allows token issuer to supply his token (called `smart token`) by embedded pricing algorithm. Each connector provides conversion between smart token and medium token of interexchange (called `connected token`).

## Build

It requires [eosio.cdt](https://github.com/EOSIO/eosio.cdt) v1.6.1 or higher. Compiled wasm and abi will be generated under `./build`.

``` console
$ ./build.sh
```

## Deploy

```console
$ cleos set contract eosbancor build/eosbancor
$ cleos set account permission eosbancor active --add-code
```

## Usage

It requires deployed [eosio.token](https://github.com/EOSIO/eosio.contracts/tree/master/contracts/eosio.token) contract or other contracts which owns `issue`, `retire` and `transfer` actions compatible with `eosio.token`.

First, initialize contract by setting connected token. (`init` can be called only once)

```console
$ cleos push action eosbancor init '["eosio", ["4,EOS", "eosio.token"]]' -p eosbancor@active
```

Second, create a connector and transfer token for initial balance. Here, smart token is issued on `conr2d.token` contract and its issuer is `conr2d`. The symbol of smart token is `HOBL`.

```console
$ cleos push action eosbancor connect '[["0,HOBL", "conr2d.token"], {"quantity": "10000.0000 EOS", "contract": "eosio.token"}, 0.5]' -p conr2d@active
$ cleos push action eosio.token transfer '["conr2d", "eosbancor", "10000.0000 EOS", "HOBL@conr2d.token"]' -p conr2d@active
$ cleos set account permission conr2d active eosbancor --add-code
```

Third, user can convert smart or connected token by trasfer token to bancor contract.

```console
// buy token
$ cleos push action eosio.token transfer '["alice", "eosbancor", "1.0000 EOS", "HOBL@conr2d.token"]' -p alice@active

// sell token
$ cleos push action conr2d.token transfer '["alice", "eosbancor", "1 HOBL", "EOS@eosio.token"]' -p alice@active
```
