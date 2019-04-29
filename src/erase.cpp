#include <eosiolib/eosio.hpp>
#include <eosiolib/print.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/action.hpp>
#include <eosiolib/symbol.hpp>
#include <math.h>
#include <eosio.token/eosio.token.hpp>

using namespace eosio;
using namespace std;

class [[eosio::contract]]  bancor : public eosio::contract {
    public:
        using contract::contract;


        //初始化数据表
        [[eosio::action]]
        void setinit() {
            ratio_index ratios(_self, _self.value);
            for(auto itr = ratios.begin(); itr != ratios.end();) {
                itr = ratios.erase(itr);
            }
        }

        //任务认领相关的表：
        struct [[eosio::table]] ratio {
            uint64_t ratioid;
            uint64_t value;        //cw 的实际值，取值范围[0-1000]，表示千分之几

            uint64_t primary_key() const {
                return ratioid;
            }
            
            EOSLIB_SERIALIZE(ratio, (ratioid)(value))
        };

        typedef eosio::multi_index<name("ratio"), ratio> ratio_index;
};


EOSIO_DISPATCH(bancor, (setinit))