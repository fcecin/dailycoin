/**
 *  Dailycoin
 *  http://github.com/fcecin/dailycoin
 */

#include <dailycoin.hpp>

namespace eosio {

   void token::create( name   issuer,
                       asset  maximum_supply )
   {
      require_auth( _self );

      auto sym = maximum_supply.symbol;
      check( sym.is_valid(), "invalid symbol name" );
      check( maximum_supply.is_valid(), "invalid supply");
      check( maximum_supply.amount > 0, "max-supply must be positive");
      check( sym.precision() == SYMBOL_PRECISION, "unsupported symbol precision");

      stats statstable( _self, sym.code().raw() );
      auto existing = statstable.find( sym.code().raw() );
      check( existing == statstable.end(), "token with symbol already exists" );

      statstable.emplace( _self, [&]( auto& s ) {
            s.supply.symbol = maximum_supply.symbol;
            s.max_supply    = maximum_supply;
            s.issuer        = issuer;
            s.burned.symbol = maximum_supply.symbol;
            s.claims        = 0;
         });
   }


   void token::issue( name to, asset quantity, string memo )
   {
      auto sym = quantity.symbol;
      check( sym.is_valid(), "invalid symbol name" );
      check( memo.size() <= 256, "memo has more than 256 bytes" );

      stats statstable( _self, sym.code().raw() );
      auto existing = statstable.find( sym.code().raw() );
      check( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
      const auto& st = *existing;

      require_auth( st.issuer );
      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must issue positive quantity" );

      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
      check( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

      statstable.modify( st, same_payer, [&]( auto& s ) {
            s.supply += quantity;
         });

      add_balance( st.issuer, quantity, st.issuer );

      if( to != st.issuer ) {
         SEND_INLINE_ACTION( *this, transfer, { {st.issuer, "active"_n} },
                             { st.issuer, to, quantity, memo }
                             );
      }
   }

   void token::retire( asset quantity, string memo )
   {
      auto sym = quantity.symbol;
      check( sym.is_valid(), "invalid symbol name" );
      check( memo.size() <= 256, "memo has more than 256 bytes" );

      stats statstable( _self, sym.code().raw() );
      auto existing = statstable.find( sym.code().raw() );
      check( existing != statstable.end(), "token with symbol does not exist" );
      const auto& st = *existing;

      if ( st.issuer != _self ) // allows anyone to retire the token
         require_auth( st.issuer );

      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must retire positive quantity" );

      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

      statstable.modify( st, same_payer, [&]( auto& s ) {
            s.supply -= quantity;
            s.burned += quantity;
         });

      sub_balance( st.issuer, quantity );
   }

   void token::transfer( name    from,
                         name    to,
                         asset   quantity,
                         string  memo )
   {
      check( from != to, "cannot transfer to self" );
      require_auth( from );
      check( is_account( to ), "to account does not exist");
      auto sym = quantity.symbol.code();
      stats statstable( _self, sym.raw() );
      const auto& st = statstable.get( sym.raw() );

      require_recipient( from );
      require_recipient( to );

      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must transfer positive quantity" );
      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
      check( memo.size() <= 256, "memo has more than 256 bytes" );

      auto payer = has_auth( to ) ? to : from;

      // always check for pending dailycoin income when using it
      try_ubi_claim( from, quantity.symbol, payer, statstable, st, false );

      sub_balance( from, quantity );
      add_balance( to, quantity, payer );
   }

   void token::open( name owner, const symbol& symbol, name ram_payer )
   {
      require_auth( ram_payer );

      auto sym_code_raw = symbol.code().raw();

      stats statstable( _self, sym_code_raw );
      const auto& st = statstable.get( sym_code_raw, "symbol does not exist" );
      check( st.supply.symbol == symbol, "symbol precision mismatch" );

      accounts acnts( _self, owner.value );
      auto it = acnts.find( sym_code_raw );
      if( it == acnts.end() ) {
         acnts.emplace( ram_payer, [&]( auto& a ){
               a.balance = asset{0, symbol};
               a.last_claim_day = 0;
            });
      }

      // reuse this call to also check for income (new accounts will start with 1 XDL)
      try_ubi_claim( owner, symbol, ram_payer, statstable, st, false );
   }

   void token::close( name owner, const symbol& symbol )
   {
      require_auth( owner );
      accounts acnts( _self, owner.value );
      auto it = acnts.find( symbol.code().raw() );
      check( it != acnts.end(), "Balance row already deleted or never existed. Action won't have any effect." );
      check( it->balance.amount == 0, "Cannot close because the balance is not zero." );
      const time_type today = get_today();
      if ( it->last_claim_day != 0 ) {
         check( it->last_claim_day < today, "Cannot close() yet: income was already claimed for today." );
      }
      check( today > last_signup_reward_day, "Cannot close() yet: must wait for the end of the reward period.");
      acnts.erase( it );
   }

   void token::claim( name owner ) {
      //require_auth( owner );
      //require_recipient( owner );

      // in case the user didn't have an open balance yet, now they will have one.
      // open() also checks for income.
      open( owner, COIN_SYMBOL, owner );

      //stats statstable( _self, COIN_SYMBOL.code().raw() );
      //const auto& st = statstable.get( COIN_SYMBOL.code().raw() );
      //
      //try_ubi_claim( owner, COIN_SYMBOL, owner, statstable, st, true );
   }

   void token::claimfor( name owner, name ram_payer )
   {
      require_recipient( owner );
      open( owner, COIN_SYMBOL, ram_payer );
   }

   void token::burn( name owner, asset quantity )
   {
      require_auth( owner );

      auto sym = quantity.symbol;
      check( sym.is_valid(), "invalid symbol name" );

      stats statstable( _self, sym.code().raw() );
      auto existing = statstable.find( sym.code().raw() );
      check( existing != statstable.end(), "token with symbol does not exist" );
      const auto& st = *existing;

      check( quantity.is_valid(), "invalid quantity" );
      check( quantity.amount > 0, "must retire positive quantity" );

      check( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

      statstable.modify( st, same_payer, [&]( auto& s ) {
            s.supply -= quantity;
            s.burned += quantity;
         });

      sub_balance( owner, quantity );
   }

   void token::income( name to, asset quantity, string memo ) {
      require_auth( _self );
      require_recipient( to );
   }

   void token::setshare( name owner, name to, uint8_t percent )
   {
      require_auth( owner );

      check( percent <= 100, "invalid percent value" );
      check( owner != to , "cannot setshare to self" );
      check( is_account( to ), "to account does not exist");

      shares stbl( _self, owner.value );
      auto it = stbl.find( to.value );
      if ( it == stbl.end() ) {
         if (percent > 0) {
            stbl.emplace( owner, [&]( auto& s ){
                  s.to      = to;
                  s.percent = percent;
              });
         }
      } else {
         if (percent > 0) {
            stbl.modify( it, same_payer, [&]( auto& s ) {
                  s.percent = percent;
              });
         } else {
            stbl.erase( it );
         }
      }

      // If share percent total exceed 100%, refuse this action
      uint64_t pcsum = 0; 
      it = stbl.begin();
      while ( it != stbl.end() ) {
        pcsum += (*it).percent;
        ++it;
      }
      check( pcsum <= 100, "share total would exceed 100%" );
   }

   void token::resetshare( name owner )
   {
      require_auth( owner );
      shares stbl( _self, owner.value );
      auto it = stbl.begin();
      while ( it != stbl.end() ) {
         it = stbl.erase( it );
      }
   }

   void token::shareincome( name from, name to, asset quantity, uint8_t percent )
   {
      require_auth( _self );
      require_recipient( from );
      require_recipient( to );
   }

   void token::setprofile( name owner, string profile )
   {
      require_auth( owner );

      uint64_t psize = profile.size();

      check( psize <= 1024, "profile has more than 1024 bytes" );

      profiles pfs( _self, owner.value );
      auto pf = pfs.find( 0 );
      if( pf == pfs.end() ) {
         if ( psize > 0 ) {
            pfs.emplace( owner, [&]( auto& p ){
                  p.profile = profile;
               });
         }
      } else {
        if ( psize > 0 ) {
           pfs.modify( pf, owner, [&]( auto& p ) {
                 p.profile = profile;
              });
        } else {
           pfs.erase( pf );
        }
      }
   }

   void token::sub_balance( name owner, asset value ) {
      accounts from_acnts( _self, owner.value );

      const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
      check( from.balance.amount >= value.amount, "overdrawn balance" );

      from_acnts.modify( from, owner, [&]( auto& a ) {
            a.balance -= value;
         });
   }

   void token::add_balance( name owner, asset value, name ram_payer )
   {
      accounts to_acnts( _self, owner.value );
      auto to = to_acnts.find( value.symbol.code().raw() );
      if( to == to_acnts.end() ) {
         to_acnts.emplace( ram_payer, [&]( auto& a ){
               a.balance = value;
               a.last_claim_day = 0;
            });
      } else {
         to_acnts.modify( to, same_payer, [&]( auto& a ) {
               a.balance += value;
            });
      }
   }

   void token::try_ubi_claim( name from, const symbol& sym, name payer, stats& statstable, const currency_stats& st, bool fail )
   {
      // *****************************************************************************
      // TODO: check if this account has verified identity
      // If it doesn't, return.
      // If fail == true, explain why the claim failed in an assert.
      // *****************************************************************************
      //
      // bool has_id = CHECK_ACCOUNT_HAS_ID ( from );
      // if (fail) {
      //   check( has_id, "please validate your account's identity to claim an income" );
      // } else {
      //   if (! has_id)
      //      return;
      // }

      accounts from_acnts( _self, from.value );
      const auto& from_account = from_acnts.get( sym.code().raw(), "no balance object found" );

      const time_type today = get_today();

      time_type curr_lcd = from_account.last_claim_day;

      if (curr_lcd >= today) {
         if (fail)
            check( false, "no pending income to claim" );
         return;
      }

      // If we are NOT in the reward period, then a last_claim_day of zero means YESTERDAY,
      //   that is, you get ONE token when you successfully claim for the first time today
      //   in a verified account.
      // Otherwise it means the user is entitled to an additional bonus which is the number
      //   of days until the bonus deadline. We force the bonus by emulating a "last claim
      //   date" value pushed into the past as needed (capped at max_past_claim_days).
      if (curr_lcd == 0) {
        curr_lcd = today - 1;
        if (today <= last_signup_reward_day) {
          int64_t bonus_days = last_signup_reward_day - today + 1;
          if (bonus_days > max_past_claim_days) {
            bonus_days = max_past_claim_days;
          }
          curr_lcd -= bonus_days;
        }
      }

      // The UBI grants 1 token per day per account.

      // Compute the claim amount relative to days elapsed since the last claim, excluding today's pay.
      // If you claimed yesterday, this is zero.
      int64_t claim_amount = today - curr_lcd - 1;

      // The limit for claiming accumulated past income is 360 days/coins. Unclaimed tokens past that
      //   one year maximum of accumulation are lost.
      time_type lost_days = 0;
      if (claim_amount > max_past_claim_days) {
         lost_days = claim_amount - max_past_claim_days;
         claim_amount = max_past_claim_days;
      }

      // Claim for one day.
      claim_amount += 1;

      asset claim_quantity = asset{claim_amount * PRECISION_MULTIPLIER, sym};

      // Respect the max_supply limit for UBI issuance (should never trigger).
      int64_t available_amount = st.max_supply.amount - st.supply.amount;
      if (claim_quantity.amount > available_amount)
         claim_quantity.set_amount(available_amount);

      time_type last_claim_day_delta = lost_days + (claim_quantity.amount / PRECISION_MULTIPLIER);

      if (claim_quantity.amount <= 0) {
         if (fail)
            check( false, "no coins" );
         return;
      }

      // Log this basic income payment with an inline "income" action.
      log_claim( from, claim_quantity, curr_lcd + last_claim_day_delta, lost_days );

      // Update the token total supply and claims count.
      statstable.modify( st, same_payer, [&]( auto& s ) {
            s.supply += claim_quantity;
            ++s.claims;
         });

      // Finally, move the claim date window proportional to the amount of days of income we claimed
      //   (and also account for days of income that have been forever lost)
      from_acnts.modify( from_account, from, [&]( auto& a ) {
            a.last_claim_day = curr_lcd + last_claim_day_delta;
         });

      // Now, the actual income payment can be *shared*, so we need to check the shares table.
      // So, we iterate over the shares table for "from" and look for (to, percent) entries to see
      //   which "to"s get which percentage of the claim_quantity.
      // The sum of percentages is expected to already not be exceeding 100. But if it does, this code
      //   just gracefully runs out of funds to distribute.
      // Each income share is logged as a shareincome action so the parties involved can understand
      //   what's going on.

      int64_t total_share_available = claim_quantity.amount;
      int64_t share_available = total_share_available;

      uint64_t pcsum = 0;
      shares stbl( _self, from.value );
      auto it = stbl.begin();
      while ( it != stbl.end() ) {
        const auto& sh = *it;

        int64_t shareamt = 0;
        pcsum += sh.percent;

        if ( pcsum >= 100 ) {
          // Last share: give everything, including all the truncation error
          shareamt = share_available;
        } else {
          // Not last share: give its percent w.r.t. to the total available
          //   for sharing, truncating the fractional part (rounds down)
          shareamt = (total_share_available * sh.percent) / 100;
        }

        // apply the shareamt
        share_available -= shareamt;
        asset share_quantity = asset{shareamt, sym};

        // log the giving and give it
        log_share( from, sh.to, share_quantity, sh.percent );
        add_balance( sh.to, share_quantity, payer );

        // If this is the last share, end the action
        if ( share_available <= 0 ) {
          return;
        }

        ++it;
      }

      // If we reach this code, then the UBI claimer has nonzero funds to receive
      claim_quantity.set_amount(share_available);
      add_balance( from, claim_quantity, payer );
   }

   // Logs the UBI claim as an "income" action that only the contract can call.
   void token::log_claim( name claimant, asset claim_quantity, time_type next_last_claim_day, time_type lost_days )
   {
      string claim_memo = "next on ";
      claim_memo.append( days_to_string(next_last_claim_day + 1) );
      if (lost_days > 0) {
         claim_memo.append(", lost ");
         claim_memo.append( std::to_string(lost_days) );
         claim_memo.append(" days of income.");
      }

      action {
         permission_level{_self, name("active")},
         _self,
         name("income"),
         income_notification_abi { .to=claimant, .quantity=claim_quantity, .memo=claim_memo }
      }.send();
   }

   // Log an UBI share.
   void token::log_share( name giver, name receiver, asset share_quantity, uint8_t share_percent )
   {
      action {
         permission_level{_self, name("active")},
         _self,
         name("shareincome"),
         shareincome_notification_abi { .from=giver, .to=receiver, .quantity=share_quantity, .percent=share_percent }
      }.send();
   }
  
   // "days" is days since epoch
   string token::days_to_string( int64_t days )
   {
      // https://stackoverflow.com/questions/7960318/math-to-convert-seconds-since-1970-into-date-and-vice-versa
      // http://howardhinnant.github.io/date_algorithms.html
      days += 719468;
      const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
      const unsigned doe = static_cast<unsigned>(days - era * 146097);       // [0, 146096]
      const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;  // [0, 399]
      const int64_t y = static_cast<int64_t>(yoe) + era * 400;
      const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);                // [0, 365]
      const unsigned mp = (5*doy + 2)/153;                                   // [0, 11]
      const unsigned d = doy - (153*mp+2)/5 + 1;                             // [1, 31]
      const unsigned m = mp + (mp < 10 ? 3 : -9);                            // [1, 12]

      string s = std::to_string(d);
      if (s.length() == 1)
         s = "0" + s;
      s.append("-");
      string ms = std::to_string(m);
      if (ms.length() == 1)
         ms = "0" + ms;
      s.append( ms );
      s.append("-");
      s.append( std::to_string(y + (m <= 2)) );
      return s;
   }

} /// namespace eosio

EOSIO_DISPATCH( eosio::token, (create)(issue)(transfer)(open)(close)(retire)(claim)(burn)(income) )
