/**
 *  Dailycoin
 *  http://github.com/fcecin/dailycoin
 */
#pragma once

#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/transaction.hpp>

#include <string>

namespace eosiosystem {
   class system_contract;
}

namespace eosio {

   using std::string;

   class [[eosio::contract("dailycoin")]] token : public contract {
   public:
      using contract::contract;

      [[eosio::action]]
         void create( name   issuer,
                      asset  maximum_supply);

      [[eosio::action]]
         void issue( name to, asset quantity, string memo );

      [[eosio::action]]
         void retire( asset quantity, string memo );

      [[eosio::action]]
         void transfer( name    from,
                        name    to,
                        asset   quantity,
                        string  memo );

      [[eosio::action]]
         void open( name owner, const symbol& symbol, name ram_payer );

      [[eosio::action]]
         void close( name owner, const symbol& symbol );

      [[eosio::action]]
         void claim( name owner ); // Implicit token symbol

      [[eosio::action]]
        void claimfor( name owner, name ram_payer ); // Implicit token symbol

      [[eosio::action]]
         void burn( name owner, asset quantity );

      [[eosio::action]]
         void income( name to, asset quantity, string memo );

      [[eosio::action]]
         void setshare( name owner, name to, uint8_t percent ); // Implicit token symbol

      [[eosio::action]]
         void resetshare( name owner ); // Implicit token symbol

      [[eosio::action]]
         void shareincome( name from, name to, asset quantity, uint8_t percent ); // Implicit token symbol

      [[eosio::action]]
         void setprofile( name owner, string profile ); // Implicit token symbol

      struct income_notification_abi {
         name        to;
         asset       quantity;
         string      memo;
      };

      struct shareincome_notification_abi {
         name        from;
         name        to;
         asset       quantity;
         uint8_t     percent;
      };

      static asset get_supply( name token_contract_account, symbol_code sym_code )
      {
         stats statstable( token_contract_account, sym_code.raw() );
         const auto& st = statstable.get( sym_code.raw() );
         return st.supply;
      }

      static asset get_balance( name token_contract_account, name owner, symbol_code sym_code )
      {
         accounts accountstable( token_contract_account, owner.value );
         const auto& ac = accountstable.get( sym_code.raw() );
         return ac.balance;
      }

      using create_action = eosio::action_wrapper<"create"_n, &token::create>;
      using issue_action = eosio::action_wrapper<"issue"_n, &token::issue>;
      using retire_action = eosio::action_wrapper<"retire"_n, &token::retire>;
      using transfer_action = eosio::action_wrapper<"transfer"_n, &token::transfer>;
      using open_action = eosio::action_wrapper<"open"_n, &token::open>;
      using close_action = eosio::action_wrapper<"close"_n, &token::close>;
      using claim_action = eosio::action_wrapper<"claim"_n, &token::claim>;
      using burn_action = eosio::action_wrapper<"burn"_n, &token::burn>;
      using income_action = eosio::action_wrapper<"income"_n, &token::income>;
      using setshare_action = eosio::action_wrapper<"setshare"_n, &token::setshare>;
      using resetshare_action = eosio::action_wrapper<"resetshare"_n, &token::resetshare>;
      using shareincome_action = eosio::action_wrapper<"shareincome"_n, &token::shareincome>;
      using setprofile_action = eosio::action_wrapper<"setprofile"_n, &token::setprofile>;

   private:

      // This contract is hard-coded to support only tokens with a precision of 4.
      static const uint8_t SYMBOL_PRECISION = 4;
      static const int64_t PRECISION_MULTIPLIER = 10000;

      // This contract is intended to be used with this single token/symbol only.
      static constexpr symbol COIN_SYMBOL = symbol("XDL", SYMBOL_PRECISION);

      typedef uint32_t time_type;

      struct [[eosio::table]] account {
         asset       balance;
         time_type   last_claim_day;

         uint64_t primary_key()const { return balance.symbol.code().raw(); }
      };

      struct [[eosio::table]] currency_stats {
         asset    supply;
         asset    max_supply;
         name     issuer;
         asset    burned;
         uint64_t claims;

         uint64_t primary_key()const { return supply.symbol.code().raw(); }
      };

      struct [[eosio::table]] share {
         name     to;
         uint8_t  percent;

         uint64_t primary_key()const { return to.value; }
      };

      struct [[eosio::table]] profile {
         string   profile;

         uint64_t primary_key()const { return 0; }
      };

      typedef eosio::multi_index< "accounts"_n, account > accounts;
      typedef eosio::multi_index< "stat"_n, currency_stats > stats;
      typedef eosio::multi_index< "shares"_n, share > shares;
      typedef eosio::multi_index< "profiles"_n, profile > profiles;

      void sub_balance( name owner, asset value );
      void add_balance( name owner, asset value, name ram_payer );

      void try_ubi_claim( name from, const symbol& sym, name payer, stats& statstable, const currency_stats& st, bool fail );

      void log_claim( name claimant, asset claim_quantity, time_type next_last_claim_day, time_type lost_days );

      void log_share( name giver, name receiver, asset share_quantity, uint8_t share_percent );

      static string days_to_string( int64_t days );

      static time_type get_today() { return (time_type)(current_time_point().sec_since_epoch() / 86400); }

      static const int64_t max_past_claim_days = 360;

      static const time_type last_signup_reward_day = 18871; // September 1st, 2021
   };

} /// namespace eosio
