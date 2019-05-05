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

        //进行 eos 与 inve 的转换
        [[eosio::action]]
        void transfer(name from, name to, asset quantity, string memo) {
            //1. 判断 to 为自己；本人交易；交易量大于 0
            print("transfer ", quantity, " from: ", from, "\t\t");
            
            if(from == _self && to == name("intervalue11") && quantity.symbol == symbol("EOS", 4)) { //提现 eos
                // TODO 需要设置阈值，取走 eos 后不能低于 X
            } else if(from == _self) { //若是自己转账的通知则放行
                return;
            } 

            if(from == name("intervalue11") && memo == "charge") {
                return;
            }
            eosio_assert(to == _self, "should transfer to 'bancor'");
            require_auth(from);    //必须是调用者本人
            eosio_assert(quantity.amount > 0, "show me the money" );
            //2. 根据 asset 判断转入的是 eos 还是 inve            
            //3. 若为 eos，则买 inve
            //4. 若为 inve，则卖 inve
            if(quantity.symbol == symbol("EOS", 4)) {
                buy(from, quantity);
            } else if(quantity.symbol == symbol("INVE",4)) {
                sell(from, quantity);
            } else {
                print("can only accept 'EOS' or 'INVE'\t\t");
                return;
            }
        }

        //更新 CW 的action：
        [[eosio::action]]
        void setratio(uint64_t cw) {
            //1. 参数验证
            require_auth(_self);    //必须是合约账户
            eosio_assert(cw <= 1000, "ratio should be less than 1000!");

            //2. 更新
            auto balance = token::get_balance(name("eosio.token"), _self, symbol_code("EOS")).amount;
            asset eos = asset(balance, symbol("EOS", 4));
            //设置 cw 之前必须给 bancor 转入 eos
            eosio_assert(balance > 0, "should charge eos before setratio!");
            
            auto supply = token::get_balance(name("intervalue11"), _self, symbol_code("INVE")).amount;
            asset inve = asset(supply, symbol("INVE", 4));

            ratio_index ratios(_self, _self.value);
            auto ratio = ratios.find(0);
            if(ratio == ratios.end()) {     //若未设置过 CW
                ratios.emplace(_self, [&](auto& r) {
                    r.ratioid = ratios.available_primary_key();
                    r.value = cw;
                    r.supply = inve;
                    r.balance = eos;
                });
            }else {     //若已经设置过 CW
                ratios.modify(ratio, _self, [&](auto &r) {
                    r.value = cw;
                });
            }
        }

        //充值 eos 或者 inve
        [[eosio::action]]
        void charge(name payer, asset quantity) {
            //1. 判断 payer 为调用者本人且仅为 intervalue11；交易量大于 0
            require_auth(payer);    //必须是调用者本人
            eosio_assert(payer == name("intervalue11"), "only 'intervalue11' can charge");
            eosio_assert(quantity.amount > 0, "show me the money" );
            
            //2. 根据 asset 判断转入的是 eos 还是 inve，并通过内联调用转账
            name account = name("");
            auto eosamount = 0;
            auto inveamount = 0;

            ratio_index ratios(_self, _self.value);
            auto ratio = ratios.find(0);
            if(ratio == ratios.end()) {     //若未设置过 CW
                ratios.emplace(_self, [&](auto& r) {
                    r.ratioid = ratios.available_primary_key();
                });
                ratio = ratios.find(0);     //查找新生成的记录
                eosamount = token::get_balance(name("eosio.token"), _self, symbol_code("EOS")).amount;
                inveamount = token::get_balance(name("intervalue11"), _self, symbol_code("INVE")).amount;
            }else {
                eosamount = (ratio -> balance).amount;
                inveamount = (ratio -> supply).amount;
            }

            //更新数据表中的 eos 余额和 inve 供应量
            if(quantity.symbol == symbol("EOS", 4)) {
                account = name("eosio.token");
                // 增加 eos 余额
                eosamount += quantity.amount;
            } else if(quantity.symbol == symbol("INVE",4)) {
                account = name("intervalue11");
                // 增加 inve 供应量
                inveamount += quantity.amount;
            }
            ratios.modify(ratio, _self, [&](auto &r) {
                r.balance = asset(eosamount, symbol("EOS", 4));
                r.supply = asset(inveamount, symbol("INVE", 4));
            });

            if(account != name("")) {
                action(
                    permission_level{ payer, name("active")},
                    account, 
                    name("transfer"),
                    std::make_tuple(
                        payer,
                        _self, 
                        quantity,
                        std::string("charge")) 
                ).send();
                print("successfully charge ", quantity, " by ", payer);
            }
        }

        //初始化数据表
        // [[eosio::action]]
        // void setinit() {
        //     ratio_index ratios(_self, _self.value);
        //     for(auto itr = ratios.begin(); itr != ratios.end();) {
        //         itr = ratios.erase(itr);
        //     }
        // }

    private:
        //购买 token 的action：
        void buy(name buyer, asset deposit) {
            //1. 参数验证
            print("buy token from: ", buyer, " of ", deposit, "\t\t");
            
            //2. 计算兑换数量
            double balance = 0;
            double supply = 0;
            double deposit_amount = deposit.amount;
            uint64_t cw = 0;

            ratio_index ratios(_self, _self.value);
            auto ratio = ratios.find(0);
            //必须先设置 CW
            eosio_assert(ratio != ratios.end(), "set ratio first!");
            cw = ratio -> value;
            print("ratio of bancor is: ", cw, "\t\t");

            //查询 bancor 合约中的 eos 抵押总量
            // auto eos = token::get_balance(name("eosio.token"), _self, symbol_code("EOS"));
            // balance = eos.amount;
            // eosio_assert(balance > 0, "should charge eos before buy inve");
            // print("balance of eos in bancor is: ", eos, "\t\t");
            balance = (ratio -> balance).amount;
            eosio_assert(balance > 0, "should charge eos before buy inve");
            print("balance of eos in bancor is: ", balance, "\t\t");


            // //查询 inve 合约中的 inve 发行总量
            // auto inve = token::get_supply(name("intervalue11"), symbol_code("INVE"));
            // supply = inve.amount;
            // print("supply of INVE is: ", inve, "\t\t");

            //查询 bancor 合约中的 inve 供应量
            supply = (ratio -> supply).amount;
            eosio_assert(supply > 0, "should charge inve before buy inve");
            print("supply of INVE is: ", supply, "\t\t");

            //计算出购买的 token 数量
            double smart_token = calculate_purchase_return(balance, deposit_amount, supply, cw);
            print("purchased token amount is: ", smart_token, "\t\t");

            // //3. 增发智能 token
            // if(smart_token > 0) {
            //     //增发 token
            //     asset inve = asset(smart_token, symbol("INVE", 4));
            //     action(
            //         permission_level{ name("intervalue11"), name("active")},
            //         name("intervalue11"), 
            //         name("issue"),
            //         std::make_tuple(
            //             buyer, 
            //             inve,
            //             std::string("issue by bancor")) 
            //     ).send();
            //     print("successfully bought ", inve, " with ", deposit);
            // }

            //3. 增发智能 token
            auto amount = supply + smart_token;
            ratios.modify(ratio, _self, [&](auto &r) {
                r.supply = asset(amount, symbol("INVE", 4));
            });

            //4. 更新 eos 余额
            amount = balance + deposit.amount;
            ratios.modify(ratio, _self, [&](auto &r) {
                r.balance = asset(amount, symbol("EOS", 4));
            });

            //5. 若 bancor 合约中 inve 足够，则转出 inve；否则从 intervalue11获取
            if(smart_token > 0) {
                auto inve_in_bancor = token::get_balance(name("intervalue11"), _self, symbol_code("INVE"));
                auto inve_amount = inve_in_bancor.amount;

                //转出 token
                asset inve = asset(smart_token, symbol("INVE", 4));
                if(inve_amount >= smart_token) {    // bancor 合约中有足够的 inve
                    action(
                        permission_level{ _self, name("active")},
                        name("intervalue11"), 
                        name("transfer"),
                        std::make_tuple(
                            _self,
                            buyer, 
                            inve,
                            std::string("for buy inve")) 
                    ).send();
                }else {
                    action(
                        permission_level{ name("intervalue11"), name("active")},
                        name("intervalue11"), 
                        name("transfer"),
                        std::make_tuple(
                            name("intervalue11"),
                            buyer, 
                            inve,
                            std::string("for buy inve")) 
                    ).send();
                }
                print("successfully bought ", inve, " with ", deposit);
            }
        }

        //卖出 token 的action：
        void sell(name seller, asset sell) {
            //1. 参数验证
            print("sell token from: ", seller, " of ", sell, "\t\t");

            //2. 计算兑换数量
            double balance = 0;
            double supply = 0;
            double sell_amount = sell.amount;
            uint64_t cw = 0;

            ratio_index ratios(_self, _self.value);
            auto ratio = ratios.find(0);
            //必须先设置 CW
            eosio_assert(ratio != ratios.end(), "set ratio first!");
            cw = ratio -> value;
            print("ratio of bancor is: ", cw, "\t\t");

            //查询 bancor 合约中的 eos 抵押总量
            // auto eos = token::get_balance(name("eosio.token"), _self, symbol_code("EOS"));
            // balance = eos.amount;
            // eosio_assert(balance > 0, "should charge eos before sell inve");
            // eosio_assert(sell.symbol == symbol("INVE", 4), "can only sell INVE");
            // print("balance of eos in bancor is: ", eos, "\t\t");
            balance = (ratio -> balance).amount;
            eosio_assert(balance > 0, "should charge eos before buy inve");
            eosio_assert(sell.symbol == symbol("INVE", 4), "can only sell INVE");
            print("balance of eos in bancor is: ", balance, "\t\t");

            // //查询 inve 合约中的 inve 发行总量
            // auto inve = token::get_supply(name("intervalue11"), symbol_code("INVE"));
            // supply = inve.amount;
            // print("supply of INVE is: ", inve, "\t\t");
            //查询 bancor 合约中的 inve 供应量
            supply = (ratio -> supply).amount;
            eosio_assert(supply > 0, "should charge inve before buy inve");
            print("supply of INVE is: ", supply, "\t\t");

            //计算出卖出 token 获得的 eos 数量
            double eos_token = calculate_sale_return(balance, sell_amount, supply, cw);
            eosio_assert(balance >= eos_token, "not enough eos for sell inve");
            print("returned eos amount is: ", eos_token, "\t\t");

            // //3. 销毁 inve 并转账 eos
            // if(eos_token > 0) {
            //     //将要销毁的 token 转给发行者
            //     if(seller != name("intervalue11")) {    //不能转给自己
            //         action(
            //             permission_level{ _self, name("active")},
            //             name("intervalue11"), 
            //             name("transfer"),
            //             std::make_tuple(
            //                 _self,
            //                 name("intervalue11"),     //issuer of inve token
            //                 sell,
            //                 std::string("send inve to be retired to issuer!\t\t")) 
            //         ).send();
            //     }
            //     //销毁 token
            //     action(
            //         permission_level{ name("intervalue11"), name("active")},
            //         name("intervalue11"), 
            //         name("retire"),
            //         std::make_tuple(
            //             sell,
            //             std::string("retire by bancor\t\t")) 
            //     ).send();

            //     //转账 EOS
            //     asset eos = asset(eos_token, symbol("EOS", 4));
            //     action(
            //         permission_level{ _self, name("active")},
            //         name("eosio.token"), 
            //         name("transfer"),
            //         std::make_tuple(
            //             _self, 
            //             seller,
            //             eos,
            //             std::string("for sell inve")) 
            //     ).send();
            //     print("successfully sold ", eos, " of ", sell);
            // }

            //3. 减少智能 token 供应量
            auto amount = supply - sell_amount;
            eosio_assert(amount > 0, "inve exceeds supply!");
            ratios.modify(ratio, _self, [&](auto &r) {
                r.supply = asset(amount, symbol("INVE", 4));
            });
            
            //4. 转账 eos
            if(eos_token > 0) {
                auto eos_in_bancor = token::get_balance(name("eosio.token"), _self, symbol_code("EOS"));
                auto eos_amount = eos_in_bancor.amount;
                eosio_assert(eos_amount >= eos_token, "not enough eos in bancor, please charge!");
                
                //更新 eos 余额
                amount = (ratio -> balance).amount - eos_token;
                eosio_assert(amount >= 0, "too much inve, not enough eos");
                ratios.modify(ratio, _self, [&](auto &r) {
                    r.balance = asset(amount, symbol("EOS", 4));
                });

                //转账 EOS
                asset eos = asset(eos_token, symbol("EOS", 4));
                action(
                    permission_level{ _self, name("active")},
                    name("eosio.token"), 
                    name("transfer"),
                    std::make_tuple(
                        _self, 
                        seller,
                        eos,
                        std::string("for sell inve")) 
                ).send();
                print("successfully sold ", eos, " of ", sell);
            }
        }

        
        double calculate_purchase_return(double balance, double deposit_amount, double supply, uint64_t ratio) {
            double R(supply);
            double C(balance - deposit_amount);
            double F = (float)ratio / 1000.0;
            double T(deposit_amount);
            double ONE(1.0);

            double E = R * (pow(ONE + T / C, F) - ONE);
            return E;
        }

        double calculate_sale_return(double balance, double sell_amount, double supply, uint64_t ratio) {
            double R(supply);
            double C(balance);
            double F = 1000.0 / (float)ratio;
            double E(sell_amount);
            double ONE(1.0);

            double T = C * (pow(ONE + E/R, F) - ONE);
            return T;
        }

        //任务认领相关的表：
        struct [[eosio::table]] ratio {
            uint64_t ratioid;
            uint64_t value;         //cw 的实际值，取值范围[0-1000]，表示千分之几
            asset supply;           //记录的 inve 供应量
            asset balance;          //记录的 eos 余额

            uint64_t primary_key() const {
                return ratioid;
            }
            
            EOSLIB_SERIALIZE(ratio, (ratioid)(value)(supply)(balance))
        };

        typedef eosio::multi_index<name("ratio"), ratio> ratio_index;
};


#define EOSIO_DISPATCH_CUSTOM(TYPE, MEMBERS) \
extern "C" { \
    void apply( uint64_t receiver, uint64_t code, uint64_t action ) { \
        auto self = receiver; \
            if( \
                (code == self  && action != name("transfer").value ) ||     \
                (code == name("eosio.token").value && action == name("transfer").value) ||  \
                action == name("onerror").value ||  \
                (code == name("intervalue11").value && action == name("transfer").value))    \
            { \
                switch( action ) { \
                    EOSIO_DISPATCH_HELPER( TYPE, MEMBERS ) \
                } \
                /* does not allow destructor of this contract to run: eosio_exit(0); */ \
            } \
    } \
} \

// EOSIO_DISPATCH_CUSTOM(bancor, (transfer)(setratio)(charge)(setinit))
EOSIO_DISPATCH_CUSTOM(bancor, (transfer)(setratio)(charge))