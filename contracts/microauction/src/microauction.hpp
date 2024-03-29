#include <math.h>

#include <eosio/eosio.hpp>
#include <eosio/transaction.hpp>
#include <eosio/asset.hpp>
#include <eosio/symbol.hpp>
#include <eosio/singleton.hpp>
#include <eosio/action.hpp>


using namespace eosio;
using std::string;
using std::vector;


// this is to access a multi-index in an external whitelist contract
struct account_row {
   name         account;    //account name
   uint8_t      score;      //enables a score between 0 to 255
   string       metadata;   //json meta data
   time_point_sec   timestamp;

   uint64_t primary_key()const { return account.value; }
   uint64_t by_timestamp() const { return uint64_t(timestamp.sec_since_epoch()); }
};
typedef eosio::multi_index<
  "accounts"_n, account_row,
  indexed_by<"bytime"_n, const_mem_fun<account_row, uint64_t, &account_row::by_timestamp>>
> accounts_score_table;


inline uint128_t ordbycycle(uint64_t cycle_number, name account) {
  return (((uint128_t)cycle_number)<<64) | account.value;
}

inline uint128_t ordbyuser(uint64_t cycle_number, name account) {
  return (((uint128_t)account.value)<<64) | cycle_number;
}



CONTRACT microauction : public eosio::contract {
    using contract::contract;
    public:

        TABLE settings {
            name            whitelist;
            name            tokens_account;
            name            savings_account;
            uint64_t        cycles;
            uint64_t        seconds_per_cycle;
            uint64_t        start_ts;
            extended_asset  quota_per_cycle;
            extended_asset  accepted_token;
            uint16_t        payouts_per_payin;  // how many outbound transfers to trigger on each inbound
            uint16_t        payouts_delay_sec;  // delay in seconds
            uint16_t        payout_cycles_per_user; // how many cycles aggregated per transfer for a user
        };

        typedef eosio::singleton<"settings"_n, settings> settings_t;



        TABLE cycle {
          asset    total_payins;
          uint64_t number;
          uint64_t primary_key()const { return number; }
        };

        typedef eosio::multi_index<"cycle"_n, cycle> cycles_t;
        
        TABLE limit {
          asset    max_per_cycle;
        };
        typedef eosio::singleton<"limit"_n, limit> limit_t;


        TABLE payment {
          uint64_t  id;
          uint64_t  cycle_number;
          name      account;
          asset     quantity;
          eosio::time_point_sec     vested_date;
          uint64_t  primary_key()const { return id; }
          uint64_t by_user()const { return account.value; }
          uint128_t get_ordbycycle()const { return ordbycycle(cycle_number, account); }
          uint128_t get_ordbyuser()const { return ordbyuser(cycle_number, account); }
        };

        typedef eosio::multi_index<"payment"_n, payment,
          indexed_by<"user"_n, const_mem_fun<payment, uint64_t, &payment::by_user>>,
          indexed_by<"ordbycycle"_n, const_mem_fun<payment, uint128_t, &payment::get_ordbycycle>>,
          indexed_by<"ordbyuser"_n, const_mem_fun<payment, uint128_t, &payment::get_ordbyuser>>
          > payments_t;



       [[eosio::action]] void init(settings setting); 

       [[eosio::action]] void setlimit(asset max_per_cycle); 


        // aggregate payments for a user
       [[eosio::action]] void claim(name payer); 


        void send_tokens_for_user(name payer, bool do_asserts); 



        // Send up to this many transfers. Anyone can trigger this action.
       [[eosio::action]] void sendtokens(uint16_t count); 



        // inline notifications
        struct receipt_abi {
          name payer;
          vector<uint64_t> cycles;
          extended_asset payout;
        };
        
       [[eosio::action]] void receipt(name payer, vector<uint64_t> cycles, extended_asset payout);


        void transfer(name from, name to, asset quantity, string memo) {
  if(to != _self)
    return;

    eosio::check( quantity.is_valid(), "invalid quantity" );
    eosio::check( is_account( from ), "from account does not exist" );
    eosio::check( memo.size() <= 256, "memo has more than 256 bytes" );

  if(memo == "seed transfer")
    return;
    
  // avoid unstaking and system contract ops mishaps
  if(   from == to || 
        from == eosio::name("eosio") || from == eosio::name("eosio.bpay") ||
        from == eosio::name("eosio.msig") || from == eosio::name("eosio.names") ||
        from == eosio::name("eosio.prods") || from == eosio::name("eosio.ram") ||
        from == eosio::name("eosio.ramfee") || from == eosio::name("eosio.saving") ||
        from == eosio::name("eosio.stake") || from == eosio::name("eosio.token") ||
        from == eosio::name("eosio.unregd") || from == eosio::name("eosio.vpay") ||
        from == eosio::name("eosio.wrap") || from == eosio::name("eosio.rex") ) {
    return;
  }
    
  require_auth(from);
  settings_t settings_table(_self, _self.value);
  auto current_settings = settings_table.get();

  // calculate current cycle
  uint64_t current_cycle = getCurrentCycle(current_settings);
  eosio::check(current_cycle < current_settings.cycles, "auction ended");

  eosio::check(quantity.symbol == current_settings.accepted_token.quantity.symbol,
               "wrong asset symbol");
  eosio::check(quantity.amount >= current_settings.accepted_token.quantity.amount,
               "below minimum amount");
  eosio::check(get_first_receiver() == current_settings.accepted_token.contract, "wrong asset contract");
  auto whitelist = current_settings.whitelist;
  // parse memo to support different account than the sending account
  //if (from == "altcoinomysa"_n){
  //   name to_act = name(memo.c_str());
  //    eosio::check(is_account(to_act), "The account name supplied is not valid");
  //    eosio::check(to_act != _self, "The account name supplied is not valid");
  //    from = to_act;
 // }

  eosio::check(isWhitelisted(from), "whitelisting required");
  registerPayment(current_settings, current_cycle, from, quantity);

  extended_asset savings;
  savings.contract = current_settings.accepted_token.contract;
  savings.quantity = quantity;
  
  save(savings);
}


    private:

      uint64_t getCurrentCycle(settings& current_settings){
        eosio::check(current_time_point().time_since_epoch().count() >= current_settings.start_ts, "auction did not start yet");
        auto elapsed_time = current_time_point().time_since_epoch().count() - current_settings.start_ts;
        return elapsed_time / (current_settings.seconds_per_cycle * 1000000 );
      }


      void check_limit(asset quantity){
        limit_t limit_table(_self, _self.value);
        if(!limit_table.exists())
          return;
        auto current_limit = limit_table.get();
        eosio::check(quantity <= current_limit.max_per_cycle, "account passed the cycle limit");
      }
      
      void registerPayment(settings& current_settings, uint64_t current_cycle, name payer, asset quantity){
        payments_t payments_table(_self, _self.value);
        cycles_t cycles_table(_self, _self.value);
        auto current_cycle_entry = cycles_table.find(current_cycle);
        if(current_cycle_entry == cycles_table.end()){
          cycles_table.emplace(_self, [&](auto &s) {
            s.number = current_cycle;
            s.total_payins = quantity;
          });
        }
        else{
          cycles_table.modify(current_cycle_entry, _self, [&](auto &s) {
            s.total_payins += quantity;
          });
        }

        auto payidx = payments_table.get_index<"ordbycycle"_n>();
        auto payitr = payidx.find(ordbycycle(current_cycle, payer));
        if( payitr == payidx.end() ) {
          payments_table.emplace(_self, [&](auto &p) {
            p.id = payments_table.available_primary_key();
            p.cycle_number = current_cycle;
            p.account = payer;
            p.quantity = quantity;
            p.vested_date = eosio::time_point_sec( current_time_point().sec_since_epoch() + current_settings.payouts_delay_sec );
            check_limit(p.quantity);
          });
        }
        else{
          eosio::check(payitr->cycle_number == current_cycle && payitr->account == payer,
                       "Retrieved a wrong payment row");
          payments_table.modify(*payitr, _self, [&](auto &p) {
            p.quantity += quantity;
            check_limit(p.quantity);
          });
        }

        if( current_settings.payouts_per_payin > 0 ) {
          // if there are any pending payouts, schedule a deferred tx
          payitr = payidx.begin();
          // no need to check for payidx.end() because we just inserted an entry
          if( payitr->cycle_number < current_cycle) {
            transaction tx;
            tx.actions.emplace_back(
                                    permission_level{_self, "active"_n},
                                    _self, "sendtokens"_n,
                                    std::make_tuple(current_settings.payouts_per_payin)
                                    );
            tx.delay_sec = current_settings.payouts_delay_sec;
            tx.send(payer.value, _self);
          }
        }
      }



      bool isWhitelisted(name payer){
        settings_t settings_table(_self, _self.value);
        auto current_settings = settings_table.get();
        auto whitelist = current_settings.whitelist;
        if(whitelist == _self)
          return true;
        accounts_score_table accounts_scores(current_settings.whitelist, current_settings.whitelist.value);
        auto existing = accounts_scores.find(payer.value);
        return (existing != accounts_scores.end() && existing->score > 70);
      }



      void transferToken(name to, extended_asset quantity){
        settings_t settings_table(_self, _self.value);
        auto current_settings = settings_table.get();

        action(permission_level{_self, "active"_n},
           quantity.contract, "transfer"_n,
           std::make_tuple(current_settings.tokens_account, to, quantity.quantity, std::string("VIG token microauction")))
        .send();
      }
      
      void save(extended_asset quantity){
        settings_t settings_table(_self, _self.value);
        auto current_settings = settings_table.get();

        action(permission_level{_self, "active"_n},
           quantity.contract, "transfer"_n,
           std::make_tuple(_self, current_settings.savings_account, quantity.quantity, std::string("Savings")))
        .send();
      }
      
};

extern "C" {
  void apply(uint64_t receiver, uint64_t code, uint64_t action) {
    if (action == "transfer"_n.value && code != receiver) {
      eosio::execute_action(eosio::name(receiver), eosio::name(code),
                            &microauction::transfer);
    }
    if (code == receiver) {
      switch (action) {
        EOSIO_DISPATCH_HELPER(microauction, (init)(sendtokens)(claim)(receipt)(setlimit))
      }
    }
    eosio_exit(0);
  }
}