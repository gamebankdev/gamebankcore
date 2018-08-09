#include <gamebank/protocol/gamebank_operations.hpp>

#include <fc/macros.hpp>
#include <fc/io/json.hpp>
#include <fc/macros.hpp>

#include <locale>

namespace gamebank { namespace protocol {

   void account_create_operation::validate() const
   {
      validate_account_name( new_account_name );
      FC_ASSERT( is_asset_type( fee, GBC_SYMBOL ), "Account creation fee must be GBC" );
      owner.validate();
      active.validate();

      if ( json_metadata.size() > 0 )
      {
         FC_ASSERT( fc::is_utf8(json_metadata), "JSON Metadata not formatted in UTF8" );
         FC_ASSERT( fc::json::is_valid(json_metadata), "JSON Metadata not valid JSON" );
      }
      FC_ASSERT( fee >= asset( 0, GBC_SYMBOL ), "Account creation fee cannot be negative" );
   }

   void account_create_with_delegation_operation::validate() const
   {
      validate_account_name( new_account_name );
      validate_account_name( creator );
      FC_ASSERT( is_asset_type( fee, GBC_SYMBOL ), "Account creation fee must be GBC" );
      FC_ASSERT( is_asset_type( delegation, GBS_SYMBOL ), "Delegation must be GBS" );

      owner.validate();
      active.validate();
      posting.validate();

      if( json_metadata.size() > 0 )
      {
         FC_ASSERT( fc::is_utf8(json_metadata), "JSON Metadata not formatted in UTF8" );
         FC_ASSERT( fc::json::is_valid(json_metadata), "JSON Metadata not valid JSON" );
      }

      FC_ASSERT( fee >= asset( 0, GBC_SYMBOL ), "Account creation fee cannot be negative" );
      FC_ASSERT( delegation >= asset( 0, GBS_SYMBOL ), "Delegation cannot be negative" );
   }

   void account_update_operation::validate() const
   {
      validate_account_name( account );
      /*if( owner )
         owner->validate();
      if( active )
         active->validate();
      if( posting )
         posting->validate();*/

      if ( json_metadata.size() > 0 )
      {
         FC_ASSERT( fc::is_utf8(json_metadata), "JSON Metadata not formatted in UTF8" );
         FC_ASSERT( fc::json::is_valid(json_metadata), "JSON Metadata not valid JSON" );
      }
   }

   void comment_operation::validate() const
   {
      FC_ASSERT( title.size() < 256, "Title larger than size limit" );
      FC_ASSERT( fc::is_utf8( title ), "Title not formatted in UTF8" );
      FC_ASSERT( body.size() > 0, "Body is empty" );
      FC_ASSERT( fc::is_utf8( body ), "Body not formatted in UTF8" );


      if( parent_author.size() )
         validate_account_name( parent_author );
      validate_account_name( author );
      validate_permlink( parent_permlink );
      validate_permlink( permlink );

      if( json_metadata.size() > 0 )
      {
         FC_ASSERT( fc::json::is_valid(json_metadata), "JSON Metadata not valid JSON" );
      }
   }

   struct comment_options_extension_validate_visitor
   {
      typedef void result_type;

      void operator()( const comment_payout_beneficiaries& cpb ) const
      {
         cpb.validate();
      }
   };

   void comment_payout_beneficiaries::validate()const
   {
      uint32_t sum = 0;

      FC_ASSERT( beneficiaries.size(), "Must specify at least one beneficiary" );
      FC_ASSERT( beneficiaries.size() < 128, "Cannot specify more than 127 beneficiaries." ); // Require size serializtion fits in one byte.

      validate_account_name( beneficiaries[0].account );
      FC_ASSERT( beneficiaries[0].weight <= GAMEBANK_100_PERCENT, "Cannot allocate more than 100% of rewards to one account" );
      sum += beneficiaries[0].weight;
      FC_ASSERT( sum <= GAMEBANK_100_PERCENT, "Cannot allocate more than 100% of rewards to a comment" ); // Have to check incrementally to avoid overflow

      for( size_t i = 1; i < beneficiaries.size(); i++ )
      {
         validate_account_name( beneficiaries[i].account );
         FC_ASSERT( beneficiaries[i].weight <= GAMEBANK_100_PERCENT, "Cannot allocate more than 100% of rewards to one account" );
         sum += beneficiaries[i].weight;
         FC_ASSERT( sum <= GAMEBANK_100_PERCENT, "Cannot allocate more than 100% of rewards to a comment" ); // Have to check incrementally to avoid overflow
         FC_ASSERT( beneficiaries[i - 1] < beneficiaries[i], "Benficiaries must be specified in sorted order (account ascending)" );
      }
   }

   void comment_options_operation::validate()const
   {
      validate_account_name( author );
      FC_ASSERT( percent_gamebank_dollars <= GAMEBANK_100_PERCENT, "Percent cannot exceed 100%" );
      FC_ASSERT( max_accepted_payout.symbol == GBD_SYMBOL, "Max accepted payout must be in GBD" );
      FC_ASSERT( max_accepted_payout.amount.value >= 0, "Cannot accept less than 0 payout" );
      validate_permlink( permlink );
      for( auto& e : extensions )
         e.visit( comment_options_extension_validate_visitor() );
   }

   void delete_comment_operation::validate()const
   {
      validate_permlink( permlink );
      validate_account_name( author );
   }

   void claim_account_operation::validate()const
   {
      validate_account_name( creator );
      FC_ASSERT( is_asset_type( fee, GBC_SYMBOL ), "Account creation fee must be GBC" );
      FC_ASSERT( fee >= asset( 0, GBC_SYMBOL ), "Account creation fee cannot be negative" );
      FC_ASSERT( extensions.size() == 0, "There are no extensions for claim_account_operation." );
   }

   void create_claimed_account_operation::validate()const
   {
      validate_account_name( creator );
      validate_account_name( new_account_name );
      owner.validate();
      active.validate();
      posting.validate();

      if( json_metadata.size() > 0 )
      {
         FC_ASSERT( fc::is_utf8(json_metadata), "JSON Metadata not formatted in UTF8" );
         FC_ASSERT( fc::json::is_valid(json_metadata), "JSON Metadata not valid JSON" );
      }

      FC_ASSERT( extensions.size() == 0, "There are no extensions for create_claimed_account_operation." );
   }

   void vote_operation::validate() const
   {
      validate_account_name( voter );
      validate_account_name( author );\
      FC_ASSERT( abs(weight) <= GAMEBANK_100_PERCENT, "Weight is not a GAMEBANK percentage" );
      validate_permlink( permlink );
   }

   void transfer_operation::validate() const
   { try {
      validate_account_name( from );
      validate_account_name( to );
      FC_ASSERT( amount.symbol != GBS_SYMBOL, "transferring of Gamebank Power (STMP) is not allowed." );
      FC_ASSERT( amount.amount > 0, "Cannot transfer a negative amount (aka: stealing)" );
      FC_ASSERT( memo.size() < GAMEBANK_MAX_MEMO_SIZE, "Memo is too large" );
      FC_ASSERT( fc::is_utf8( memo ), "Memo is not UTF8" );
   } FC_CAPTURE_AND_RETHROW( (*this) ) }

   void transfer_to_vesting_operation::validate() const
   {
      validate_account_name( from );
      FC_ASSERT( amount.symbol == GBC_SYMBOL, "Amount must be GBC" );
      if ( to != account_name_type() ) validate_account_name( to );
      FC_ASSERT( amount.amount > 0, "Must transfer a nonzero amount" );
   }

   void withdraw_vesting_operation::validate() const
   {
      validate_account_name( account );
      FC_ASSERT( is_asset_type( vesting_shares, GBS_SYMBOL), "Amount must be GBS"  );
   }

   void set_withdraw_vesting_route_operation::validate() const
   {
      validate_account_name( from_account );
      validate_account_name( to_account );
      FC_ASSERT( 0 <= percent && percent <= GAMEBANK_100_PERCENT, "Percent must be valid gbc percent" );
   }

   void witness_update_operation::validate() const
   {
      validate_account_name( owner );

      FC_ASSERT( url.size() <= GAMEBANK_MAX_WITNESS_URL_LENGTH, "URL is too long" );

      FC_ASSERT( url.size() > 0, "URL size must be greater than 0" );
      FC_ASSERT( fc::is_utf8( url ), "URL is not valid UTF8" );
      FC_ASSERT( fee >= asset( 0, GBC_SYMBOL ), "Fee cannot be negative" );
      props.validate< false >();
   }

   void witness_set_properties_operation::validate() const
   {
      validate_account_name( owner );

      // current signing key must be present
      FC_ASSERT( props.find( "key" ) != props.end(), "No signing key provided" );

      auto itr = props.find( "account_creation_fee" );
      if( itr != props.end() )
      {
         asset account_creation_fee;
         fc::raw::unpack_from_vector( itr->second, account_creation_fee );
         FC_ASSERT( account_creation_fee.symbol == GBC_SYMBOL, "account_creation_fee must be in GBC" );
         FC_ASSERT( account_creation_fee.amount >= GAMEBANK_MIN_ACCOUNT_CREATION_FEE , "account_creation_fee smaller than minimum account creation fee" );
      }

      itr = props.find( "maximum_block_size" );
      if( itr != props.end() )
      {
         uint32_t maximum_block_size;
         fc::raw::unpack_from_vector( itr->second, maximum_block_size );
         FC_ASSERT( maximum_block_size >= GAMEBANK_MIN_BLOCK_SIZE_LIMIT, "maximum_block_size smaller than minimum max block size" );
      }

      itr = props.find( "gbd_interest_rate" );
      if( itr != props.end() )
      {
         uint16_t gbd_interest_rate;
         fc::raw::unpack_from_vector( itr->second, gbd_interest_rate );
         FC_ASSERT( gbd_interest_rate >= 0, "gbd_interest_rate must be positive" );
         FC_ASSERT( gbd_interest_rate <= GAMEBANK_100_PERCENT, "gbd_interest_rate must not exceed 100%" );
      }

      itr = props.find( "new_signing_key" );
      if( itr != props.end() )
      {
         public_key_type signing_key;
         fc::raw::unpack_from_vector( itr->second, signing_key );
         FC_UNUSED( signing_key ); // This tests the deserialization of the key
      }

      itr = props.find( "gbd_exchange_rate" );
      if( itr != props.end() )
      {
         price gbd_exchange_rate;
         fc::raw::unpack_from_vector( itr->second, gbd_exchange_rate );
         FC_ASSERT( ( is_asset_type( gbd_exchange_rate.base, GBD_SYMBOL ) && is_asset_type( gbd_exchange_rate.quote, GBC_SYMBOL ) ),
            "Price feed must be a GBC/GBD price" );
         gbd_exchange_rate.validate();
      }

      itr = props.find( "url" );
      if( itr != props.end() )
      {
         std::string url;
         fc::raw::unpack_from_vector< std::string >( itr->second, url );

         FC_ASSERT( url.size() <= GAMEBANK_MAX_WITNESS_URL_LENGTH, "URL is too long" );
         FC_ASSERT( url.size() > 0, "URL size must be greater than 0" );
         FC_ASSERT( fc::is_utf8( url ), "URL is not valid UTF8" );
      }

      itr = props.find( "account_subsidy_limit" );
      if( itr != props.end() )
      {
         uint32_t account_subsidy_limit;
         fc::raw::unpack_from_vector( itr->second, account_subsidy_limit ); // Checks that the value can be deserialized
         FC_UNUSED( account_subsidy_limit );
      }
   }

   void account_witness_vote_operation::validate() const
   {
      validate_account_name( account );
      validate_account_name( witness );
   }

   void account_witness_proxy_operation::validate() const
   {
      validate_account_name( account );
      if( proxy.size() )
         validate_account_name( proxy );
      FC_ASSERT( proxy != account, "Cannot proxy to self" );
   }

   void custom_operation::validate() const {
      /// required auth accounts are the ones whose bandwidth is consumed
      FC_ASSERT( required_auths.size() > 0, "at least on account must be specified" );
   }
   void custom_json_operation::validate() const {
      /// required auth accounts are the ones whose bandwidth is consumed
      FC_ASSERT( (required_auths.size() + required_posting_auths.size()) > 0, "at least on account must be specified" );
      FC_ASSERT( id.size() <= 32, "id is too long" );
      FC_ASSERT( fc::is_utf8(json), "JSON Metadata not formatted in UTF8" );
      FC_ASSERT( fc::json::is_valid(json), "JSON Metadata not valid JSON" );
   }
   void custom_binary_operation::validate() const {
      /// required auth accounts are the ones whose bandwidth is consumed
      FC_ASSERT( (required_owner_auths.size() + required_active_auths.size() + required_posting_auths.size()) > 0, "at least on account must be specified" );
      FC_ASSERT( id.size() <= 32, "id is too long" );
      for( const auto& a : required_auths ) a.validate();
   }


   fc::sha256 pow_operation::work_input()const
   {
      auto hash = fc::sha256::hash( block_id );
      hash._hash[0] = nonce;
      return fc::sha256::hash( hash );
   }

   void pow_operation::validate()const
   {
      props.validate< true >();
      validate_account_name( worker_account );
      FC_ASSERT( work_input() == work.input, "Determninistic input does not match recorded input" );
      work.validate();
   }

   struct pow2_operation_validate_visitor
   {
      typedef void result_type;

      template< typename PowType >
      void operator()( const PowType& pow )const
      {
         pow.validate();
      }
   };

   void pow2_operation::validate()const
   {
      props.validate< true >();
      work.visit( pow2_operation_validate_visitor() );
   }

   struct pow2_operation_get_required_active_visitor
   {
      typedef void result_type;

      pow2_operation_get_required_active_visitor( flat_set< account_name_type >& required_active )
         : _required_active( required_active ) {}

      template< typename PowType >
      void operator()( const PowType& work )const
      {
         _required_active.insert( work.input.worker_account );
      }

      flat_set<account_name_type>& _required_active;
   };

   void pow2_operation::get_required_active_authorities( flat_set<account_name_type>& a )const
   {
      if( !new_owner_key )
      {
         pow2_operation_get_required_active_visitor vtor( a );
         work.visit( vtor );
      }
   }

   void pow::create( const fc::ecc::private_key& w, const digest_type& i )
   {
      input  = i;
      signature = w.sign_compact(input,false);

      auto sig_hash            = fc::sha256::hash( signature );
      public_key_type recover  = fc::ecc::public_key( signature, sig_hash, false );

      work = fc::sha256::hash(recover);
   }
   void pow2::create( const block_id_type& prev, const account_name_type& account_name, uint64_t n )
   {
      input.worker_account = account_name;
      input.prev_block     = prev;
      input.nonce          = n;

      auto prv_key = fc::sha256::hash( input );
      auto input = fc::sha256::hash( prv_key );
      auto signature = fc::ecc::private_key::regenerate( prv_key ).sign_compact(input);

      auto sig_hash            = fc::sha256::hash( signature );
      public_key_type recover  = fc::ecc::public_key( signature, sig_hash );

      fc::sha256 work = fc::sha256::hash(std::make_pair(input,recover));
      pow_summary = work.approx_log_32();
   }

   void equihash_pow::create( const block_id_type& recent_block, const account_name_type& account_name, uint32_t nonce )
   {
      input.worker_account = account_name;
      input.prev_block = recent_block;
      input.nonce = nonce;

      auto seed = fc::sha256::hash( input );
      proof = fc::equihash::proof::hash( GAMEBANK_EQUIHASH_N, GAMEBANK_EQUIHASH_K, seed );
      pow_summary = fc::sha256::hash( proof.inputs ).approx_log_32();
   }

   void pow::validate()const
   {
      FC_ASSERT( work != fc::sha256() );
      FC_ASSERT( public_key_type(fc::ecc::public_key( signature, input, false )) == worker );
      auto sig_hash = fc::sha256::hash( signature );
      public_key_type recover  = fc::ecc::public_key( signature, sig_hash, false );
      FC_ASSERT( work == fc::sha256::hash(recover) );
   }

   void pow2::validate()const
   {
      validate_account_name( input.worker_account );
      pow2 tmp; tmp.create( input.prev_block, input.worker_account, input.nonce );
      FC_ASSERT( pow_summary == tmp.pow_summary, "reported work does not match calculated work" );
   }

   void equihash_pow::validate() const
   {
      validate_account_name( input.worker_account );
      auto seed = fc::sha256::hash( input );
      FC_ASSERT( proof.n == GAMEBANK_EQUIHASH_N, "proof of work 'n' value is incorrect" );
      FC_ASSERT( proof.k == GAMEBANK_EQUIHASH_K, "proof of work 'k' value is incorrect" );
      FC_ASSERT( proof.seed == seed, "proof of work seed does not match expected seed" );
      FC_ASSERT( proof.is_valid(), "proof of work is not a solution", ("block_id", input.prev_block)("worker_account", input.worker_account)("nonce", input.nonce) );
      FC_ASSERT( pow_summary == fc::sha256::hash( proof.inputs ).approx_log_32() );
   }

   void feed_publish_operation::validate()const
   {
      validate_account_name( publisher );
      FC_ASSERT( ( is_asset_type( exchange_rate.base, GBC_SYMBOL ) && is_asset_type( exchange_rate.quote, GBD_SYMBOL ) )
         || ( is_asset_type( exchange_rate.base, GBD_SYMBOL ) && is_asset_type( exchange_rate.quote, GBC_SYMBOL ) ),
         "Price feed must be a GBC/GBD price" );
      exchange_rate.validate();
   }

   void limit_order_create_operation::validate()const
   {
      validate_account_name( owner );

      FC_ASSERT(  ( is_asset_type( amount_to_sell, GBC_SYMBOL ) && is_asset_type( min_to_receive, GBD_SYMBOL ) )
               || ( is_asset_type( amount_to_sell, GBD_SYMBOL ) && is_asset_type( min_to_receive, GBC_SYMBOL ) ),
               "Limit order must be for the GBC:GBD market" );

      (amount_to_sell / min_to_receive).validate();
   }

   void limit_order_create2_operation::validate()const
   {
      validate_account_name( owner );

      FC_ASSERT( amount_to_sell.symbol == exchange_rate.base.symbol, "Sell asset must be the base of the price" );
      exchange_rate.validate();

      FC_ASSERT(  ( is_asset_type( amount_to_sell, GBC_SYMBOL ) && is_asset_type( exchange_rate.quote, GBD_SYMBOL ) )
               || ( is_asset_type( amount_to_sell, GBD_SYMBOL ) && is_asset_type( exchange_rate.quote, GBC_SYMBOL ) ),
               "Limit order must be for the GBC:GBD market" );

      FC_ASSERT( (amount_to_sell * exchange_rate).amount > 0, "Amount to sell cannot round to 0 when traded" );
   }

   void limit_order_cancel_operation::validate()const
   {
      validate_account_name( owner );
   }

   void convert_operation::validate()const
   {
      validate_account_name( owner );
      /// only allow conversion from GBD to GBC, allowing the opposite can enable traders to abuse
      /// market fluxuations through converting large quantities without moving the price.
      FC_ASSERT( is_asset_type( amount, GBD_SYMBOL ), "Can only convert GBD to GBC" );
      FC_ASSERT( amount.amount > 0, "Must convert some GBD" );
   }

   void report_over_production_operation::validate()const
   {
      validate_account_name( reporter );
      validate_account_name( first_block.witness );
      FC_ASSERT( first_block.witness   == second_block.witness );
      FC_ASSERT( first_block.timestamp == second_block.timestamp );
      FC_ASSERT( first_block.signee()  == second_block.signee() );
      FC_ASSERT( first_block.id() != second_block.id() );
   }

   void escrow_transfer_operation::validate()const
   {
      validate_account_name( from );
      validate_account_name( to );
      validate_account_name( agent );
      FC_ASSERT( fee.amount >= 0, "fee cannot be negative" );
      FC_ASSERT( gbd_amount.amount >= 0, "gbd amount cannot be negative" );
      FC_ASSERT( gbc_amount.amount >= 0, "gbc amount cannot be negative" );
      FC_ASSERT( gbd_amount.amount > 0 || gbc_amount.amount > 0, "escrow must transfer a non-zero amount" );
      FC_ASSERT( from != agent && to != agent, "agent must be a third party" );
      FC_ASSERT( (fee.symbol == GBC_SYMBOL) || (fee.symbol == GBD_SYMBOL), "fee must be GBC or GBD" );
      FC_ASSERT( gbd_amount.symbol == GBD_SYMBOL, "gbd amount must contain GBD" );
      FC_ASSERT( gbc_amount.symbol == GBC_SYMBOL, "gbc amount must contain GBC" );
      FC_ASSERT( ratification_deadline < escrow_expiration, "ratification deadline must be before escrow expiration" );
      if ( json_meta.size() > 0 )
      {
         FC_ASSERT( fc::is_utf8(json_meta), "JSON Metadata not formatted in UTF8" );
         FC_ASSERT( fc::json::is_valid(json_meta), "JSON Metadata not valid JSON" );
      }
   }

   void escrow_approve_operation::validate()const
   {
      validate_account_name( from );
      validate_account_name( to );
      validate_account_name( agent );
      validate_account_name( who );
      FC_ASSERT( who == to || who == agent, "to or agent must approve escrow" );
   }

   void escrow_dispute_operation::validate()const
   {
      validate_account_name( from );
      validate_account_name( to );
      validate_account_name( agent );
      validate_account_name( who );
      FC_ASSERT( who == from || who == to, "who must be from or to" );
   }

   void escrow_release_operation::validate()const
   {
      validate_account_name( from );
      validate_account_name( to );
      validate_account_name( agent );
      validate_account_name( who );
      validate_account_name( receiver );
      FC_ASSERT( who == from || who == to || who == agent, "who must be from or to or agent" );
      FC_ASSERT( receiver == from || receiver == to, "receiver must be from or to" );
      FC_ASSERT( gbd_amount.amount >= 0, "gbd amount cannot be negative" );
      FC_ASSERT( gbc_amount.amount >= 0, "gbc amount cannot be negative" );
      FC_ASSERT( gbd_amount.amount > 0 || gbc_amount.amount > 0, "escrow must release a non-zero amount" );
      FC_ASSERT( gbd_amount.symbol == GBD_SYMBOL, "gbd amount must contain GBD" );
      FC_ASSERT( gbc_amount.symbol == GBC_SYMBOL, "gbc amount must contain GBC" );
   }

   void request_account_recovery_operation::validate()const
   {
      validate_account_name( recovery_account );
      validate_account_name( account_to_recover );
      new_owner_authority.validate();
   }

   void recover_account_operation::validate()const
   {
      validate_account_name( account_to_recover );
      FC_ASSERT( !( new_owner_authority == recent_owner_authority ), "Cannot set new owner authority to the recent owner authority" );
      FC_ASSERT( !new_owner_authority.is_impossible(), "new owner authority cannot be impossible" );
      FC_ASSERT( !recent_owner_authority.is_impossible(), "recent owner authority cannot be impossible" );
      FC_ASSERT( new_owner_authority.weight_threshold, "new owner authority cannot be trivial" );
      new_owner_authority.validate();
      recent_owner_authority.validate();
   }

   void change_recovery_account_operation::validate()const
   {
      validate_account_name( account_to_recover );
      validate_account_name( new_recovery_account );
   }

   void transfer_to_savings_operation::validate()const {
      validate_account_name( from );
      validate_account_name( to );
      FC_ASSERT( amount.amount > 0 );
      FC_ASSERT( amount.symbol == GBC_SYMBOL || amount.symbol == GBD_SYMBOL );
      FC_ASSERT( memo.size() < GAMEBANK_MAX_MEMO_SIZE, "Memo is too large" );
      FC_ASSERT( fc::is_utf8( memo ), "Memo is not UTF8" );
   }
   void transfer_from_savings_operation::validate()const {
      validate_account_name( from );
      validate_account_name( to );
      FC_ASSERT( amount.amount > 0 );
      FC_ASSERT( amount.symbol == GBC_SYMBOL || amount.symbol == GBD_SYMBOL );
      FC_ASSERT( memo.size() < GAMEBANK_MAX_MEMO_SIZE, "Memo is too large" );
      FC_ASSERT( fc::is_utf8( memo ), "Memo is not UTF8" );
   }
   void cancel_transfer_from_savings_operation::validate()const {
      validate_account_name( from );
   }

   void decline_voting_rights_operation::validate()const
   {
      validate_account_name( account );
   }

   void reset_account_operation::validate()const
   {
      validate_account_name( reset_account );
      validate_account_name( account_to_reset );
      FC_ASSERT( !new_owner_authority.is_impossible(), "new owner authority cannot be impossible" );
      FC_ASSERT( new_owner_authority.weight_threshold, "new owner authority cannot be trivial" );
      new_owner_authority.validate();
   }

   void set_reset_account_operation::validate()const
   {
      validate_account_name( account );
      if( current_reset_account.size() )
         validate_account_name( current_reset_account );
      validate_account_name( reset_account );
      FC_ASSERT( current_reset_account != reset_account, "new reset account cannot be current reset account" );
   }

   void claim_reward_balance_operation::validate()const
   {
      validate_account_name( account );
      FC_ASSERT( is_asset_type( reward_gbc, GBC_SYMBOL ), "Reward must be GBC" );
      FC_ASSERT( is_asset_type( reward_gbd, GBD_SYMBOL ), "Reward must be GBD" );
      FC_ASSERT( is_asset_type( reward_vests, GBS_SYMBOL ), "Reward must be GBS" );
      FC_ASSERT( reward_gbc.amount >= 0, "Cannot claim a negative amount" );
      FC_ASSERT( reward_gbd.amount >= 0, "Cannot claim a negative amount" );
      FC_ASSERT( reward_vests.amount >= 0, "Cannot claim a negative amount" );
      FC_ASSERT( reward_gbc.amount > 0 || reward_gbd.amount > 0 || reward_vests.amount > 0, "Must claim something." );
   }

   void delegate_vesting_shares_operation::validate()const
   {
      validate_account_name( delegator );
      validate_account_name( delegatee );
      FC_ASSERT( delegator != delegatee, "You cannot delegate GBS to yourself" );
      FC_ASSERT( is_asset_type( vesting_shares, GBS_SYMBOL ), "Delegation must be GBS" );
      FC_ASSERT( vesting_shares >= asset( 0, GBS_SYMBOL ), "Delegation cannot be negative" );
   }

   void crowdfunding_operation::validate()const
   {
       FC_ASSERT( title.size() < 256, "Title larger than size limit" );
       FC_ASSERT( fc::is_utf8( title ), "Title not formatted in UTF8" );
       FC_ASSERT( body.size() > 0, "Body is empty" );
       FC_ASSERT( fc::is_utf8( body ), "Body not formatted in UTF8" );

       validate_account_name( originator );
       validate_permlink( permlink );

       if ( json_metadata.size() > 0 )
       {
           FC_ASSERT( fc::json::is_valid( json_metadata ), "JSON Metadata not valid JSON");
       }

       FC_ASSERT( is_asset_type( raise, GBC_SYMBOL ), "Raise must be GBC_SYMBOL");
       FC_ASSERT( raise >= asset( 0, GBC_SYMBOL ), "Raise cannot be negative");
   }

   void invest_operation::validate()const
   {
       validate_account_name( invester );

       validate_account_name( originator );
       validate_permlink( permlink );

       FC_ASSERT( is_asset_type( raise, GBC_SYMBOL ), "Invest must be GBC_SYMBOL");
       FC_ASSERT( raise >= asset( 0, GBC_SYMBOL ), "Invest cannot be negative");
   }

   void nonfungible_fund_create_operation::validate()const
   {
	   FC_ASSERT(meta_data.size() > 0, "meta_data is empty");
	   FC_ASSERT(meta_data.size() < 256, "meta_data larger than size limit");
	   FC_ASSERT(fc::is_utf8(meta_data), "meta_data not formatted in UTF8");

	   validate_account_name(creator);
	   validate_account_name(owner);
   }

   void nonfungible_fund_transfer_operation::validate()const
   {
	   FC_ASSERT(fund_id > 0, "fund_id is zero");
	   FC_ASSERT(from != to, "from cant equal to");

	   validate_account_name(from);
	   validate_account_name(to);
   }

   void nonfungible_fund_put_up_for_sale_operation::validate()const
   {
       FC_ASSERT( fund_id > 0, "fund_id is zero" );
       FC_ASSERT(  is_asset_type( selling_price, GBC_SYMBOL )    || is_asset_type( selling_price, GBD_SYMBOL ),"Price must be for the GBC or GBD" );
       FC_ASSERT( selling_price.amount > 0, "Price should > 0" );
     
       validate_account_name( seller );
   }

   void nonfungible_fund_withdraw_from_sale_operation::validate()const
   {
       FC_ASSERT( fund_id > 0, "fund_id is zero" );

       validate_account_name( seller );
   }

   void nonfungible_fund_buy_operation::validate()const
   {
       FC_ASSERT( fund_id > 0, "fund_id is zero" );

       validate_account_name( buyer );
   }

   void contract_deploy_operation::validate()const
   {
	   validate_account_name(creator);
	   FC_ASSERT(code.size() > 0, "code is empty");
	   FC_ASSERT(code.size() < 1024*1024, "code larger than size limit");
	   FC_ASSERT(abi.size() > 0, "abi is empty");
	   FC_ASSERT(abi.size() < 1024 * 64, "abi larger than size limit");
   }

   void contract_call_operation::validate()const
   {
	   validate_account_name(contract_name);
	   validate_account_name(caller);
	   FC_ASSERT(method.size() > 0, "method is empty");
	   FC_ASSERT(method.size() < 256, "method larger than size limit");
	   FC_ASSERT(args.size() > 0, "args is empty");
	   FC_ASSERT(args.size() < 1024 * 64, "args larger than size limit");
   }

} } // gamebank::protocol
