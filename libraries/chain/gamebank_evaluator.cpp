#include <gamebank/chain/gamebank_evaluator.hpp>
#include <gamebank/chain/database.hpp>
#include <gamebank/chain/custom_operation_interpreter.hpp>
#include <gamebank/chain/gamebank_objects.hpp>
#include <gamebank/chain/witness_objects.hpp>
#include <gamebank/chain/block_summary_object.hpp>

#include <gamebank/chain/util/reward.hpp>

#include <fc/macros.hpp>

#ifndef IS_LOW_MEM
#ifndef _WIN32
FC_TODO( "After we vendor fc, also vendor diff_match_patch and fix these warnings" )
#endif
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic push
#if !defined( __clang__ )
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <diff_match_patch.h>
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop
#include <boost/locale/encoding_utf.hpp>

using boost::locale::conv::utf_to_utf;

std::wstring utf8_to_wstring(const std::string& str)
{
    return utf_to_utf<wchar_t>(str.c_str(), str.c_str() + str.size());
}

std::string wstring_to_utf8(const std::wstring& str)
{
    return utf_to_utf<char>(str.c_str(), str.c_str() + str.size());
}

#endif

#include <fc/uint128.hpp>
#include <fc/utf8.hpp>

#include <limits>

namespace gamebank { namespace chain {
   using fc::uint128_t;

inline void validate_permlink_0_1( const string& permlink )
{
   FC_ASSERT( permlink.size() > GAMEBANK_MIN_PERMLINK_LENGTH && permlink.size() < GAMEBANK_MAX_PERMLINK_LENGTH, "Permlink is not a valid size." );

   for( const auto& c : permlink )
   {
      switch( c )
      {
         case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'h': case 'i':
         case 'j': case 'k': case 'l': case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
         case 's': case 't': case 'u': case 'v': case 'w': case 'x': case 'y': case 'z': case '0':
         case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
         case '-':
            break;
         default:
            FC_ASSERT( false, "Invalid permlink character: ${s}", ("s", std::string() + c ) );
      }
   }
}

struct strcmp_equal
{
   bool operator()( const shared_string& a, const string& b )
   {
      return a.size() == b.size() || std::strcmp( a.c_str(), b.c_str() ) == 0;
   }
};

template< bool force_canon >
void copy_legacy_chain_properties( chain_properties& dest, const legacy_chain_properties& src )
{
   dest.account_creation_fee = src.account_creation_fee.to_asset< force_canon >();
   dest.maximum_block_size = src.maximum_block_size;
   dest.gbd_interest_rate = src.gbd_interest_rate;
}

void witness_update_evaluator::do_apply( const witness_update_operation& o )
{
   _db.get_account( o.owner ); // verify owner exists

   FC_ASSERT( o.props.account_creation_fee.symbol.is_canon() );
   
   #pragma message( "TODO: This needs to be part of HF 20 and moved to validate if not triggered in previous blocks" )
   if( _db.is_producing() )
   {
      FC_ASSERT( o.props.maximum_block_size <= GAMEBANK_SOFT_MAX_BLOCK_SIZE, "Max block size cannot be more than 2MiB" );
   }

   #pragma message( "TODO: This needs to be part of HF 20 and moved to validate if not triggered in previous blocks" )
   if( _db.is_producing() )
   {
      FC_ASSERT( o.props.maximum_block_size <= GAMEBANK_SOFT_MAX_BLOCK_SIZE, "Max block size cannot be more than 2MiB" );
   }

   const auto& by_witness_name_idx = _db.get_index< witness_index >().indices().get< by_name >();
   auto wit_itr = by_witness_name_idx.find( o.owner );
   if( wit_itr != by_witness_name_idx.end() )
   {
      _db.modify( *wit_itr, [&]( witness_object& w ) {
         from_string( w.url, o.url );
         w.signing_key        = o.block_signing_key;
         copy_legacy_chain_properties< false >( w.props, o.props );
      });
   }
   else
   {
      _db.create< witness_object >( [&]( witness_object& w ) {
         w.owner              = o.owner;
         from_string( w.url, o.url );
         w.signing_key        = o.block_signing_key;
         w.created            = _db.head_block_time();
         copy_legacy_chain_properties< false >( w.props, o.props );
      });
   }
}

void witness_set_properties_evaluator::do_apply( const witness_set_properties_operation& o )
{
   FC_ASSERT( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ), "witness_set_properties_evaluator not enabled until HF 20" );

   const auto& witness = _db.get< witness_object, by_name >( o.owner ); // verifies witness exists;

   // Capture old properties. This allows only updating the object once.
   chain_properties  props;
   public_key_type   signing_key;
   price             gbd_exchange_rate;
   time_point_sec    last_gbd_exchange_update;
   string            url;

   bool account_creation_changed = false;
   bool max_block_changed        = false;
   bool gbd_interest_changed     = false;
   bool account_subsidy_changed  = false;
   bool key_changed              = false;
   bool gbd_exchange_changed     = false;
   bool url_changed              = false;

   auto itr = o.props.find( "key" );

   // This existence of 'key' is checked in witness_set_properties_operation::validate
   fc::raw::unpack_from_vector( itr->second, signing_key );
   FC_ASSERT( signing_key == witness.signing_key, "'key' does not match witness signing key.",
      ("key", signing_key)("signing_key", witness.signing_key) );

   itr = o.props.find( "account_creation_fee" );
   if( itr != o.props.end() )
   {
      fc::raw::unpack_from_vector( itr->second, props.account_creation_fee );
      account_creation_changed = true;
   }

   itr = o.props.find( "maximum_block_size" );
   if( itr != o.props.end() )
   {
      fc::raw::unpack_from_vector( itr->second, props.maximum_block_size );
      max_block_changed = true;
   }

   itr = o.props.find( "gbd_interest_rate" );
   if( itr != o.props.end() )
   {
      fc::raw::unpack_from_vector( itr->second, props.gbd_interest_rate );
      gbd_interest_changed = true;
   }

   itr = o.props.find( "account_subsidy_limit" );
   if( itr != o.props.end() )
   {
      fc::raw::unpack_from_vector( itr->second, props.account_subsidy_limit );
      account_subsidy_changed = true;
   }

   itr = o.props.find( "new_signing_key" );
   if( itr != o.props.end() )
   {
      fc::raw::unpack_from_vector( itr->second, signing_key );
      key_changed = true;
   }

   itr = o.props.find( "gbd_exchange_rate" );
   if( itr != o.props.end() )
   {
      fc::raw::unpack_from_vector( itr->second, gbd_exchange_rate );
      last_gbd_exchange_update = _db.head_block_time();
      gbd_exchange_changed = true;
   }

   itr = o.props.find( "url" );
   if( itr != o.props.end() )
   {
      fc::raw::unpack_from_vector< std::string >( itr->second, url );
      url_changed = true;
   }

   _db.modify( witness, [&]( witness_object& w )
   {
      if( account_creation_changed )
         w.props.account_creation_fee = props.account_creation_fee;

      if( max_block_changed )
         w.props.maximum_block_size = props.maximum_block_size;

      if( gbd_interest_changed )
         w.props.gbd_interest_rate = props.gbd_interest_rate;

      if( account_subsidy_changed )
         w.props.account_subsidy_limit = props.account_subsidy_limit;

      if( key_changed )
         w.signing_key = signing_key;

      if( gbd_exchange_changed )
      {
         w.gbd_exchange_rate = gbd_exchange_rate;
         w.last_gbd_exchange_update = last_gbd_exchange_update;
      }

      if( url_changed )
         from_string( w.url, url );
   });
}

void verify_authority_accounts_exist(
   const database& db,
   const authority& auth,
   const account_name_type& auth_account,
   authority::classification auth_class)
{
   for( const std::pair< account_name_type, weight_type >& aw : auth.account_auths )
   {
      const account_object* a = db.find_account( aw.first );
      FC_ASSERT( a != nullptr, "New ${ac} authority on account ${aa} references non-existing account ${aref}",
         ("aref", aw.first)("ac", auth_class)("aa", auth_account) );
   }
}

void initialize_account_object( account_object& acc, const account_name_type& name, const public_key_type& key,
   const dynamic_global_property_object& props, bool mined, const account_name_type& recovery_account, uint32_t hardfork )
{
   acc.name = name;
   acc.memo_key = key;
   acc.created = props.time;
   acc.last_vote_time = props.time;
   acc.mined = mined;

   FC_TODO( "If after HF 20, there are no temp account creations, the HF check can be removed." )
   if( ( hardfork < GAMEBANK_HARDFORK_0_1 ) || ( recovery_account != GAMEBANK_TEMP_ACCOUNT ) )
   {
      acc.recovery_account = recovery_account;
   }
}

void account_create_evaluator::do_apply( const account_create_operation& o )
{
   const auto& creator = _db.get_account( o.creator );

   const auto& props = _db.get_dynamic_global_properties();

   FC_ASSERT( creator.balance >= o.fee, "Insufficient balance to create account.", ( "creator.balance", creator.balance )( "required", o.fee ) );

   if( !_db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) )
   {
      const witness_schedule_object& wso = _db.get_witness_schedule_object();
      FC_ASSERT( o.fee >= asset( wso.median_props.account_creation_fee.amount * GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER, GBC_SYMBOL ), "Insufficient Fee: ${f} required, ${p} provided.",
                 ("f", wso.median_props.account_creation_fee * asset( GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER, GBC_SYMBOL ) )
                 ("p", o.fee) );
   }
   else
   {
      const witness_schedule_object& wso = _db.get_witness_schedule_object();
      FC_ASSERT( o.fee >= wso.median_props.account_creation_fee, "Insufficient Fee: ${f} required, ${p} provided.",
                 ("f", wso.median_props.account_creation_fee)
                 ("p", o.fee) );
   }

   verify_authority_accounts_exist( _db, o.owner, o.new_account_name, authority::owner );
   verify_authority_accounts_exist( _db, o.active, o.new_account_name, authority::active );
   verify_authority_accounts_exist( _db, o.posting, o.new_account_name, authority::posting );

   _db.modify( creator, [&]( account_object& c ){
      c.balance -= o.fee;
   });

   const auto& new_account = _db.create< account_object >( [&]( account_object& acc )
   {
      initialize_account_object( acc, o.new_account_name, o.memo_key, props, false /*mined*/, o.creator, _db.get_hardfork() );
      #ifndef IS_LOW_MEM
         from_string( acc.json_metadata, o.json_metadata );
      #endif
   });

   _db.create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = o.new_account_name;
      auth.owner = o.owner;
      auth.active = o.active;
      auth.posting = o.posting;
      auth.last_owner_update = fc::time_point_sec::min();
   });

   if( o.fee.amount > 0 )
      _db.create_vesting( new_account, o.fee );
}

void account_create_with_delegation_evaluator::do_apply( const account_create_with_delegation_operation& o )
{
   FC_ASSERT( !_db.has_hardfork( GAMEBANK_HARDFORK_0_1 ), "Account creation with delegation is deprecated as of Hardfork 20" );

   const auto& creator = _db.get_account( o.creator );
   const auto& props = _db.get_dynamic_global_properties();
   const witness_schedule_object& wso = _db.get_witness_schedule_object();

   FC_ASSERT( creator.balance >= o.fee, "Insufficient balance to create account.",
               ( "creator.balance", creator.balance )
               ( "required", o.fee ) );

   FC_ASSERT( creator.vesting_shares - creator.delegated_vesting_shares - asset( creator.to_withdraw - creator.withdrawn, GBS_SYMBOL ) >= o.delegation, "Insufficient vesting shares to delegate to new account.",
               ( "creator.vesting_shares", creator.vesting_shares )
               ( "creator.delegated_vesting_shares", creator.delegated_vesting_shares )( "required", o.delegation ) );

   auto target_delegation = asset( wso.median_props.account_creation_fee.amount * GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER * GAMEBANK_CREATE_ACCOUNT_DELEGATION_RATIO, GBC_SYMBOL ) * props.get_vesting_share_price();

   auto current_delegation = asset( o.fee.amount * GAMEBANK_CREATE_ACCOUNT_DELEGATION_RATIO, GBC_SYMBOL ) * props.get_vesting_share_price() + o.delegation;

   FC_ASSERT( current_delegation >= target_delegation, "Inssufficient Delegation ${f} required, ${p} provided.",
               ("f", target_delegation )
               ( "p", current_delegation )
               ( "account_creation_fee", wso.median_props.account_creation_fee )
               ( "o.fee", o.fee )
               ( "o.delegation", o.delegation ) );

   FC_ASSERT( o.fee >= wso.median_props.account_creation_fee, "Insufficient Fee: ${f} required, ${p} provided.",
               ("f", wso.median_props.account_creation_fee)
               ("p", o.fee) );

   for( const auto& a : o.owner.account_auths )
   {
      _db.get_account( a.first );
   }

   for( const auto& a : o.active.account_auths )
   {
      _db.get_account( a.first );
   }

   for( const auto& a : o.posting.account_auths )
   {
      _db.get_account( a.first );
   }

   _db.modify( creator, [&]( account_object& c )
   {
      c.balance -= o.fee;
      c.delegated_vesting_shares += o.delegation;
   });

   const auto& new_account = _db.create< account_object >( [&]( account_object& acc )
   {
      initialize_account_object( acc, o.new_account_name, o.memo_key, props, false /*mined*/, o.creator, _db.get_hardfork() );
      acc.received_vesting_shares = o.delegation;

      #ifndef IS_LOW_MEM
         from_string( acc.json_metadata, o.json_metadata );
      #endif
   });

   _db.create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = o.new_account_name;
      auth.owner = o.owner;
      auth.active = o.active;
      auth.posting = o.posting;
      auth.last_owner_update = fc::time_point_sec::min();
   });

   if( o.delegation.amount > 0 )
   {
      _db.create< vesting_delegation_object >( [&]( vesting_delegation_object& vdo )
      {
         vdo.delegator = o.creator;
         vdo.delegatee = o.new_account_name;
         vdo.vesting_shares = o.delegation;
         vdo.min_delegation_time = _db.head_block_time() + GAMEBANK_CREATE_ACCOUNT_DELEGATION_TIME;
      });
   }

   if( o.fee.amount > 0 )
      _db.create_vesting( new_account, o.fee );
}


void account_update_evaluator::do_apply( const account_update_operation& o )
{
   FC_ASSERT( o.account != GAMEBANK_TEMP_ACCOUNT, "Cannot update temp account." );

   if( o.posting )
      o.posting->validate();

   const auto& account = _db.get_account( o.account );
   const auto& account_auth = _db.get< account_authority_object, by_account >( o.account );

   if( o.owner )
   {
#ifndef IS_TEST_NET
         FC_ASSERT( _db.head_block_time() - account_auth.last_owner_update > GAMEBANK_OWNER_UPDATE_LIMIT, "Owner authority can only be updated once an hour." );
#endif

      verify_authority_accounts_exist( _db, *o.owner, o.account, authority::owner );

      _db.update_owner_authority( account, *o.owner );
   }
   if( o.active    )
      verify_authority_accounts_exist( _db, *o.active, o.account, authority::active );
   if( o.posting    )
      verify_authority_accounts_exist( _db, *o.posting, o.account, authority::posting );

   _db.modify( account, [&]( account_object& acc )
   {
      if( o.memo_key != public_key_type() )
            acc.memo_key = o.memo_key;

      acc.last_account_update = _db.head_block_time();

      #ifndef IS_LOW_MEM
        if ( o.json_metadata.size() > 0 )
            from_string( acc.json_metadata, o.json_metadata );
      #endif
   });

   if( o.active || o.posting )
   {
      _db.modify( account_auth, [&]( account_authority_object& auth)
      {
         if( o.active )  auth.active  = *o.active;
         if( o.posting ) auth.posting = *o.posting;
      });
   }

}


/**
 *  Because net_rshares is 0 there is no need to update any pending payout calculations or parent posts.
 */
void delete_comment_evaluator::do_apply( const delete_comment_operation& o )
{
   const auto& comment = _db.get_comment( o.author, o.permlink );
   FC_ASSERT( comment.children == 0, "Cannot delete a comment with replies." );

   FC_ASSERT( comment.cashout_time != fc::time_point_sec::maximum() );

   FC_ASSERT( comment.net_rshares <= 0, "Cannot delete a comment with net positive votes." );

   if( comment.net_rshares > 0 ) return;

   const auto& vote_idx = _db.get_index<comment_vote_index>().indices().get<by_comment_voter>();

   auto vote_itr = vote_idx.lower_bound( comment_id_type(comment.id) );
   while( vote_itr != vote_idx.end() && vote_itr->comment == comment.id ) {
      const auto& cur_vote = *vote_itr;
      ++vote_itr;
      _db.remove(cur_vote);
   }

   /// this loop can be skiped for validate-only nodes as it is merely gathering stats for indicies
   if( comment.parent_author != GAMEBANK_ROOT_POST_PARENT )
   {
      auto parent = &_db.get_comment( comment.parent_author, comment.parent_permlink );
      auto now = _db.head_block_time();
      while( parent )
      {
         _db.modify( *parent, [&]( comment_object& p ){
            p.children--;
            p.active = now;
         });
   #ifndef IS_LOW_MEM
         if( parent->parent_author != GAMEBANK_ROOT_POST_PARENT )
            parent = &_db.get_comment( parent->parent_author, parent->parent_permlink );
         else
   #endif
            parent = nullptr;
      }
   }

   _db.remove( comment );
}

struct comment_options_extension_visitor
{
   comment_options_extension_visitor( const comment_object& c, database& db ) : _c( c ), _db( db ) {}

   typedef void result_type;

   const comment_object& _c;
   database& _db;

   void operator()( const comment_payout_beneficiaries& cpb ) const
   {
      FC_ASSERT( _c.beneficiaries.size() == 0, "Comment already has beneficiaries specified." );
      FC_ASSERT( _c.abs_rshares == 0, "Comment must not have been voted on before specifying beneficiaries." );

      _db.modify( _c, [&]( comment_object& c )
      {
         for( auto& b : cpb.beneficiaries )
         {
            auto acc = _db.find< account_object, by_name >( b.account );
            FC_ASSERT( acc != nullptr, "Beneficiary \"${a}\" must exist.", ("a", b.account) );
            c.beneficiaries.push_back( b );
         }
      });
   }
};

void comment_options_evaluator::do_apply( const comment_options_operation& o )
{
   const auto& comment = _db.get_comment( o.author, o.permlink );
   if( !o.allow_curation_rewards || !o.allow_votes || o.max_accepted_payout < comment.max_accepted_payout )
      FC_ASSERT( comment.abs_rshares == 0, "One of the included comment options requires the comment to have no rshares allocated to it." );

   FC_ASSERT( comment.allow_curation_rewards >= o.allow_curation_rewards, "Curation rewards cannot be re-enabled." );
   FC_ASSERT( comment.allow_votes >= o.allow_votes, "Voting cannot be re-enabled." );
   FC_ASSERT( comment.max_accepted_payout >= o.max_accepted_payout, "A comment cannot accept a greater payout." );
   FC_ASSERT( comment.percent_gamebank_dollars >= o.percent_gamebank_dollars, "A comment cannot accept a greater percent GBD." );

   _db.modify( comment, [&]( comment_object& c ) {
       c.max_accepted_payout   = o.max_accepted_payout;
       c.percent_gamebank_dollars = o.percent_gamebank_dollars;
       c.allow_votes           = o.allow_votes;
       c.allow_curation_rewards = o.allow_curation_rewards;
   });

   for( auto& e : o.extensions )
   {
      e.visit( comment_options_extension_visitor( comment, _db ) );
   }
}

void comment_evaluator::do_apply( const comment_operation& o )
{ try {
   FC_ASSERT( o.title.size() + o.body.size() + o.json_metadata.size(), "Cannot update comment because nothing appears to be changing." );

   const auto& by_permlink_idx = _db.get_index< comment_index >().indices().get< by_permlink >();
   auto itr = by_permlink_idx.find( boost::make_tuple( o.author, o.permlink ) );

   const auto& auth = _db.get_account( o.author ); /// prove it exists

   comment_id_type id;

   const comment_object* parent = nullptr;
   if( o.parent_author != GAMEBANK_ROOT_POST_PARENT )
   {
      parent = &_db.get_comment( o.parent_author, o.parent_permlink );

      FC_ASSERT( parent->depth < GAMEBANK_MAX_COMMENT_DEPTH, "Comment is nested ${x} posts deep, maximum depth is ${y}.", ("x",parent->depth)("y",GAMEBANK_MAX_COMMENT_DEPTH) );
   }

   FC_ASSERT( fc::is_utf8( o.json_metadata ), "JSON Metadata must be UTF-8" );

   auto now = _db.head_block_time();

   if ( itr == by_permlink_idx.end() )
   {
      if( o.parent_author != GAMEBANK_ROOT_POST_PARENT )
      {
         FC_ASSERT( _db.get( parent->root_comment ).allow_replies, "The parent comment has disabled replies." );
      }

      FC_TODO( "Cleanup this logic after HF 20. Old ops don't need to check pre-hf20 times." )
      if( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) )
      {
         if( o.parent_author == GAMEBANK_ROOT_POST_PARENT )
             FC_ASSERT( ( now - auth.last_root_post ) > GAMEBANK_MIN_ROOT_COMMENT_INTERVAL, "You may only post once every 5 minutes.", ("now",now)("last_root_post", auth.last_root_post) );
         else
             FC_ASSERT( (now - auth.last_post) >= GAMEBANK_MIN_REPLY_INTERVAL_HF01, "You may only comment once every 3 seconds.", ("now",now)("auth.last_post",auth.last_post) );
      }
      else
      {
         if( o.parent_author == GAMEBANK_ROOT_POST_PARENT )
             FC_ASSERT( ( now - auth.last_root_post ) > GAMEBANK_MIN_ROOT_COMMENT_INTERVAL, "You may only post once every 5 minutes.", ("now",now)("last_root_post", auth.last_root_post) );
         else
             FC_ASSERT( (now - auth.last_post) > GAMEBANK_MIN_REPLY_INTERVAL, "You may only comment once every 20 seconds.", ("now",now)("auth.last_post",auth.last_post) );
      }

      uint16_t reward_weight = GAMEBANK_100_PERCENT;
      uint64_t post_bandwidth = auth.post_bandwidth;

      _db.modify( auth, [&]( account_object& a ) {
         if( o.parent_author == GAMEBANK_ROOT_POST_PARENT )
         {
            a.last_root_post = now;
            a.post_bandwidth = uint32_t( post_bandwidth );
         }
         a.last_post = now;
         a.post_count++;
      });

      const auto& new_comment = _db.create< comment_object >( [&]( comment_object& com )
      {

         validate_permlink_0_1( o.parent_permlink );
         validate_permlink_0_1( o.permlink );

         com.author = o.author;
         from_string( com.permlink, o.permlink );
         com.last_update = _db.head_block_time();
         com.created = com.last_update;
         com.active = com.last_update;
         com.last_payout = fc::time_point_sec::min();
         com.max_cashout_time = fc::time_point_sec::maximum();
         com.reward_weight = reward_weight;

         if ( o.parent_author == GAMEBANK_ROOT_POST_PARENT )
         {
            com.parent_author = "";
            from_string( com.parent_permlink, o.parent_permlink );
            from_string( com.category, o.parent_permlink );
            com.root_comment = com.id;
            com.cashout_time =   _db.head_block_time() + GAMEBANK_CASHOUT_WINDOW_SECONDS_OLD;

         }
         else
         {
            com.parent_author = parent->author;
            com.parent_permlink = parent->permlink;
            com.depth = parent->depth + 1;
            com.category = parent->category;
            com.root_comment = parent->root_comment;
            com.cashout_time = fc::time_point_sec::maximum();
         }


         com.cashout_time = com.created + GAMEBANK_CASHOUT_WINDOW_SECONDS;

      });

      id = new_comment.id;

   #ifndef IS_LOW_MEM
      _db.create< comment_content_object >( [&]( comment_content_object& con )
      {
         con.comment = id;

         from_string( con.title, o.title );
         if( o.body.size() < 1024*1024*128 )
         {
            from_string( con.body, o.body );
         }
         from_string( con.json_metadata, o.json_metadata );
      });
   #endif


/// this loop can be skiped for validate-only nodes as it is merely gathering stats for indicies
      auto now = _db.head_block_time();
      while( parent ) {
         _db.modify( *parent, [&]( comment_object& p ){
            p.children++;
            p.active = now;
         });
#ifndef IS_LOW_MEM
         if( parent->parent_author != GAMEBANK_ROOT_POST_PARENT )
            parent = &_db.get_comment( parent->parent_author, parent->parent_permlink );
         else
#endif
            parent = nullptr;
      }

   }
   else // start edit case
   {
      const auto& comment = *itr;

      _db.modify( comment, [&]( comment_object& com )
      {
         com.last_update   = _db.head_block_time();
         com.active        = com.last_update;
         strcmp_equal equal;

         if( !parent )
         {
            FC_ASSERT( com.parent_author == account_name_type(), "The parent of a comment cannot change." );
            FC_ASSERT( equal( com.parent_permlink, o.parent_permlink ), "The permlink of a comment cannot change." );
         }
         else
         {
            FC_ASSERT( com.parent_author == o.parent_author, "The parent of a comment cannot change." );
            FC_ASSERT( equal( com.parent_permlink, o.parent_permlink ), "The permlink of a comment cannot change." );
         }
      });
   #ifndef IS_LOW_MEM
      _db.modify( _db.get< comment_content_object, by_comment >( comment.id ), [&]( comment_content_object& con )
      {
         if( o.title.size() )         from_string( con.title, o.title );
         if( o.json_metadata.size() )
            from_string( con.json_metadata, o.json_metadata );

         if( o.body.size() ) {
            try {
            diff_match_patch<std::wstring> dmp;
            auto patch = dmp.patch_fromText( utf8_to_wstring(o.body) );
            if( patch.size() ) {
               auto result = dmp.patch_apply( patch, utf8_to_wstring( to_string( con.body ) ) );
               auto patched_body = wstring_to_utf8(result.first);
               if( !fc::is_utf8( patched_body ) ) {
                  idump(("invalid utf8")(patched_body));
                  from_string( con.body, fc::prune_invalid_utf8(patched_body) );
               } else { from_string( con.body, patched_body ); }
            }
            else { // replace
               from_string( con.body, o.body );
            }
            } catch ( ... ) {
               from_string( con.body, o.body );
            }
         }
      });
   #endif



   } // end EDIT case

} FC_CAPTURE_AND_RETHROW( (o) ) }

void escrow_transfer_evaluator::do_apply( const escrow_transfer_operation& o )
{
   try
   {
      const auto& from_account = _db.get_account(o.from);
      _db.get_account(o.to);
      _db.get_account(o.agent);

      FC_ASSERT( o.ratification_deadline > _db.head_block_time(), "The escorw ratification deadline must be after head block time." );
      FC_ASSERT( o.escrow_expiration > _db.head_block_time(), "The escrow expiration must be after head block time." );

      asset gbc_spent = o.gbc_amount;
      asset gbd_spent = o.gbd_amount;
      if( o.fee.symbol == GBC_SYMBOL )
         gbc_spent += o.fee;
      else
         gbd_spent += o.fee;

      FC_ASSERT( from_account.balance >= gbc_spent, "Account cannot cover GBC costs of escrow. Required: ${r} Available: ${a}", ("r",gbc_spent)("a",from_account.balance) );
      FC_ASSERT( from_account.gbd_balance >= gbd_spent, "Account cannot cover GBD costs of escrow. Required: ${r} Available: ${a}", ("r",gbd_spent)("a",from_account.gbd_balance) );

      _db.adjust_balance( from_account, -gbc_spent );
      _db.adjust_balance( from_account, -gbd_spent );

      _db.create<escrow_object>([&]( escrow_object& esc )
      {
         esc.escrow_id              = o.escrow_id;
         esc.from                   = o.from;
         esc.to                     = o.to;
         esc.agent                  = o.agent;
         esc.ratification_deadline  = o.ratification_deadline;
         esc.escrow_expiration      = o.escrow_expiration;
         esc.gbd_balance            = o.gbd_amount;
         esc.gbc_balance          = o.gbc_amount;
         esc.pending_fee            = o.fee;
      });
   }
   FC_CAPTURE_AND_RETHROW( (o) )
}

void escrow_approve_evaluator::do_apply( const escrow_approve_operation& o )
{
   try
   {

      const auto& escrow = _db.get_escrow( o.from, o.escrow_id );

      FC_ASSERT( escrow.to == o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", escrow.to) );
      FC_ASSERT( escrow.agent == o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).", ("o", o.agent)("e", escrow.agent) );
      FC_ASSERT( escrow.ratification_deadline >= _db.head_block_time(), "The escrow ratification deadline has passed. Escrow can no longer be ratified." );

      bool reject_escrow = !o.approve;

      if( o.who == o.to )
      {
         FC_ASSERT( !escrow.to_approved, "Account 'to' (${t}) has already approved the escrow.", ("t", o.to) );

         if( !reject_escrow )
         {
            _db.modify( escrow, [&]( escrow_object& esc )
            {
               esc.to_approved = true;
            });
         }
      }
      if( o.who == o.agent )
      {
         FC_ASSERT( !escrow.agent_approved, "Account 'agent' (${a}) has already approved the escrow.", ("a", o.agent) );

         if( !reject_escrow )
         {
            _db.modify( escrow, [&]( escrow_object& esc )
            {
               esc.agent_approved = true;
            });
         }
      }

      if( reject_escrow )
      {
         _db.adjust_balance( o.from, escrow.gbc_balance );
         _db.adjust_balance( o.from, escrow.gbd_balance );
         _db.adjust_balance( o.from, escrow.pending_fee );

         _db.remove( escrow );
      }
      else if( escrow.to_approved && escrow.agent_approved )
      {
         _db.adjust_balance( o.agent, escrow.pending_fee );

         _db.modify( escrow, [&]( escrow_object& esc )
         {
            esc.pending_fee.amount = 0;
         });
      }
   }
   FC_CAPTURE_AND_RETHROW( (o) )
}

void escrow_dispute_evaluator::do_apply( const escrow_dispute_operation& o )
{
   try
   {
      _db.get_account( o.from ); // Verify from account exists

      const auto& e = _db.get_escrow( o.from, o.escrow_id );
      FC_ASSERT( _db.head_block_time() < e.escrow_expiration, "Disputing the escrow must happen before expiration." );
      FC_ASSERT( e.to_approved && e.agent_approved, "The escrow must be approved by all parties before a dispute can be raised." );
      FC_ASSERT( !e.disputed, "The escrow is already under dispute." );
      FC_ASSERT( e.to == o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", e.to) );
      FC_ASSERT( e.agent == o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).", ("o", o.agent)("e", e.agent) );

      _db.modify( e, [&]( escrow_object& esc )
      {
         esc.disputed = true;
      });
   }
   FC_CAPTURE_AND_RETHROW( (o) )
}

void escrow_release_evaluator::do_apply( const escrow_release_operation& o )
{
   try
   {
      _db.get_account(o.from); // Verify from account exists

      const auto& e = _db.get_escrow( o.from, o.escrow_id );
      FC_ASSERT( e.gbc_balance >= o.gbc_amount, "Release amount exceeds escrow balance. Amount: ${a}, Balance: ${b}", ("a", o.gbc_amount)("b", e.gbc_balance) );
      FC_ASSERT( e.gbd_balance >= o.gbd_amount, "Release amount exceeds escrow balance. Amount: ${a}, Balance: ${b}", ("a", o.gbd_amount)("b", e.gbd_balance) );
      FC_ASSERT( e.to == o.to, "Operation 'to' (${o}) does not match escrow 'to' (${e}).", ("o", o.to)("e", e.to) );
      FC_ASSERT( e.agent == o.agent, "Operation 'agent' (${a}) does not match escrow 'agent' (${e}).", ("o", o.agent)("e", e.agent) );
      FC_ASSERT( o.receiver == e.from || o.receiver == e.to, "Funds must be released to 'from' (${f}) or 'to' (${t})", ("f", e.from)("t", e.to) );
      FC_ASSERT( e.to_approved && e.agent_approved, "Funds cannot be released prior to escrow approval." );

      // If there is a dispute regardless of expiration, the agent can release funds to either party
      if( e.disputed )
      {
         FC_ASSERT( o.who == e.agent, "Only 'agent' (${a}) can release funds in a disputed escrow.", ("a", e.agent) );
      }
      else
      {
         FC_ASSERT( o.who == e.from || o.who == e.to, "Only 'from' (${f}) and 'to' (${t}) can release funds from a non-disputed escrow", ("f", e.from)("t", e.to) );

         if( e.escrow_expiration > _db.head_block_time() )
         {
            // If there is no dispute and escrow has not expired, either party can release funds to the other.
            if( o.who == e.from )
            {
               FC_ASSERT( o.receiver == e.to, "Only 'from' (${f}) can release funds to 'to' (${t}).", ("f", e.from)("t", e.to) );
            }
            else if( o.who == e.to )
            {
               FC_ASSERT( o.receiver == e.from, "Only 'to' (${t}) can release funds to 'from' (${t}).", ("f", e.from)("t", e.to) );
            }
         }
      }
      // If escrow expires and there is no dispute, either party can release funds to either party.

      _db.adjust_balance( o.receiver, o.gbc_amount );
      _db.adjust_balance( o.receiver, o.gbd_amount );

      _db.modify( e, [&]( escrow_object& esc )
      {
         esc.gbc_balance -= o.gbc_amount;
         esc.gbd_balance -= o.gbd_amount;
      });

      if( e.gbc_balance.amount == 0 && e.gbd_balance.amount == 0 )
      {
         _db.remove( e );
      }
   }
   FC_CAPTURE_AND_RETHROW( (o) )
}

void transfer_evaluator::do_apply( const transfer_operation& o )
{
   FC_ASSERT( _db.get_balance( o.from, o.amount.symbol ) >= o.amount, "Account does not have sufficient funds for transfer." );
   _db.adjust_balance( o.from, -o.amount );
   _db.adjust_balance( o.to, o.amount );
}

void transfer_to_vesting_evaluator::do_apply( const transfer_to_vesting_operation& o )
{
   const auto& from_account = _db.get_account(o.from);
   const auto& to_account = o.to.size() ? _db.get_account(o.to) : from_account;

   FC_ASSERT( _db.get_balance( from_account, o.amount.symbol) >= o.amount,
              "Account does not have sufficient liquid amount for transfer." );
   _db.adjust_balance( from_account, -o.amount );
   _db.create_vesting( to_account, o.amount );
}

void withdraw_vesting_evaluator::do_apply( const withdraw_vesting_operation& o )
{
   const auto& account = _db.get_account( o.account );

   FC_ASSERT( account.vesting_shares >= asset( 0, GBS_SYMBOL ), "Account does not have sufficient Gamebank Power for withdraw." );
   FC_ASSERT( account.vesting_shares - account.delegated_vesting_shares >= o.vesting_shares, "Account does not have sufficient Gamebank Power for withdraw." );

   FC_TODO( "Remove this entire block after HF 20" )
   if( !_db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) && !account.mined )
   {
      const auto& props = _db.get_dynamic_global_properties();
      const witness_schedule_object& wso = _db.get_witness_schedule_object();

      asset min_vests = wso.median_props.account_creation_fee * props.get_vesting_share_price();
      min_vests.amount.value *= 10;

      FC_ASSERT( account.vesting_shares > min_vests || ( o.vesting_shares.amount == 0 ),
                 "Account registered by another account requires 10x account creation fee worth of Gamebank Power before it can be powered down." );
   }

   if( o.vesting_shares.amount == 0 )
   {
      FC_ASSERT( account.vesting_withdraw_rate.amount  != 0, "This operation would not change the vesting withdraw rate." );

      _db.modify( account, [&]( account_object& a ) {
         a.vesting_withdraw_rate = asset( 0, GBS_SYMBOL );
         a.next_vesting_withdrawal = time_point_sec::maximum();
         a.to_withdraw = 0;
         a.withdrawn = 0;
      });
   }
   else
   {    
      int  vesting_withdraw_intervals = GAMEBANK_VESTING_WITHDRAW_INTERVALS; /// 13 weeks = 1 quarter of a year

      _db.modify( account, [&]( account_object& a )
      {
         auto new_vesting_withdraw_rate = asset( o.vesting_shares.amount / vesting_withdraw_intervals, GBS_SYMBOL );

         if( new_vesting_withdraw_rate.amount == 0 )
            new_vesting_withdraw_rate.amount = 1;

         FC_ASSERT( account.vesting_withdraw_rate  != new_vesting_withdraw_rate, "This operation would not change the vesting withdraw rate." );

         a.vesting_withdraw_rate = new_vesting_withdraw_rate;
         a.next_vesting_withdrawal = _db.head_block_time() + fc::seconds(GAMEBANK_VESTING_WITHDRAW_INTERVAL_SECONDS);
         a.to_withdraw = o.vesting_shares.amount;
         a.withdrawn = 0;
      });
   }
}

void set_withdraw_vesting_route_evaluator::do_apply( const set_withdraw_vesting_route_operation& o )
{
   try
   {
   const auto& from_account = _db.get_account( o.from_account );
   const auto& to_account = _db.get_account( o.to_account );
   const auto& wd_idx = _db.get_index< withdraw_vesting_route_index >().indices().get< by_withdraw_route >();
   auto itr = wd_idx.find( boost::make_tuple( from_account.name, to_account.name ) );

   if( itr == wd_idx.end() )
   {
      FC_ASSERT( o.percent != 0, "Cannot create a 0% destination." );
      FC_ASSERT( from_account.withdraw_routes < GAMEBANK_MAX_WITHDRAW_ROUTES, "Account already has the maximum number of routes." );

      _db.create< withdraw_vesting_route_object >( [&]( withdraw_vesting_route_object& wvdo )
      {
         wvdo.from_account = from_account.name;
         wvdo.to_account = to_account.name;
         wvdo.percent = o.percent;
         wvdo.auto_vest = o.auto_vest;
      });

      _db.modify( from_account, [&]( account_object& a )
      {
         a.withdraw_routes++;
      });
   }
   else if( o.percent == 0 )
   {
      _db.remove( *itr );

      _db.modify( from_account, [&]( account_object& a )
      {
         a.withdraw_routes--;
      });
   }
   else
   {
      _db.modify( *itr, [&]( withdraw_vesting_route_object& wvdo )
      {
         wvdo.from_account = from_account.name;
         wvdo.to_account = to_account.name;
         wvdo.percent = o.percent;
         wvdo.auto_vest = o.auto_vest;
      });
   }

   itr = wd_idx.upper_bound( boost::make_tuple( from_account.name, account_name_type() ) );
   uint16_t total_percent = 0;

   while( itr->from_account == from_account.name && itr != wd_idx.end() )
   {
      total_percent += itr->percent;
      ++itr;
   }

   FC_ASSERT( total_percent <= GAMEBANK_100_PERCENT, "More than 100% of vesting withdrawals allocated to destinations." );
   }
   FC_CAPTURE_AND_RETHROW()
}

void account_witness_proxy_evaluator::do_apply( const account_witness_proxy_operation& o )
{
   const auto& account = _db.get_account( o.account );
   FC_ASSERT( account.proxy != o.proxy, "Proxy must change." );

   FC_ASSERT( account.can_vote, "Account has declined the ability to vote and cannot proxy votes." );

   /// remove all current votes
   std::array<share_type, GAMEBANK_MAX_PROXY_RECURSION_DEPTH+1> delta;
   delta[0] = -account.vesting_shares.amount;
   for( int i = 0; i < GAMEBANK_MAX_PROXY_RECURSION_DEPTH; ++i )
      delta[i+1] = -account.proxied_vsf_votes[i];
   _db.adjust_proxied_witness_votes( account, delta );

   if( o.proxy.size() ) {
      const auto& new_proxy = _db.get_account( o.proxy );
      flat_set<account_id_type> proxy_chain( { account.id, new_proxy.id } );
      proxy_chain.reserve( GAMEBANK_MAX_PROXY_RECURSION_DEPTH + 1 );

      /// check for proxy loops and fail to update the proxy if it would create a loop
      auto cprox = &new_proxy;
      while( cprox->proxy.size() != 0 ) {
         const auto next_proxy = _db.get_account( cprox->proxy );
         FC_ASSERT( proxy_chain.insert( next_proxy.id ).second, "This proxy would create a proxy loop." );
         cprox = &next_proxy;
         FC_ASSERT( proxy_chain.size() <= GAMEBANK_MAX_PROXY_RECURSION_DEPTH, "Proxy chain is too long." );
      }

      /// clear all individual vote records
      _db.clear_witness_votes( account );

      _db.modify( account, [&]( account_object& a ) {
         a.proxy = o.proxy;
      });

      /// add all new votes
      for( int i = 0; i <= GAMEBANK_MAX_PROXY_RECURSION_DEPTH; ++i )
         delta[i] = -delta[i];
      _db.adjust_proxied_witness_votes( account, delta );
   } else { /// we are clearing the proxy which means we simply update the account
      _db.modify( account, [&]( account_object& a ) {
          a.proxy = o.proxy;
      });
   }
}


void account_witness_vote_evaluator::do_apply( const account_witness_vote_operation& o )
{
   const auto& voter = _db.get_account( o.account );
   FC_ASSERT( voter.proxy.size() == 0, "A proxy is currently set, please clear the proxy before voting for a witness." );

   if( o.approve )
      FC_ASSERT( voter.can_vote, "Account has declined its voting rights." );

   const auto& witness = _db.get_witness( o.witness );

   const auto& by_account_witness_idx = _db.get_index< witness_vote_index >().indices().get< by_account_witness >();
   auto itr = by_account_witness_idx.find( boost::make_tuple( voter.name, witness.owner ) );

   if( itr == by_account_witness_idx.end() ) {
      FC_ASSERT( o.approve, "Vote doesn't exist, user must indicate a desire to approve witness." );


         FC_ASSERT( voter.witnesses_voted_for < GAMEBANK_MAX_ACCOUNT_WITNESS_VOTES, "Account has voted for too many witnesses." ); // TODO: Remove after hardfork 2

         _db.create<witness_vote_object>( [&]( witness_vote_object& v ) {
             v.witness = witness.owner;
             v.account = voter.name;
         });


      _db.adjust_witness_vote( witness, voter.witness_vote_weight() );

      
      _db.modify( voter, [&]( account_object& a ) {
         a.witnesses_voted_for++;
      });

   } else {
      FC_ASSERT( !o.approve, "Vote currently exists, user must indicate a desire to reject witness." );



      _db.adjust_witness_vote( witness, -voter.witness_vote_weight() );

      
      _db.modify( voter, [&]( account_object& a ) {
         a.witnesses_voted_for--;
      });
      _db.remove( *itr );
   }
}


void vote_evaluator::do_apply( const vote_operation& o )
{ try {
   const auto& comment = _db.get_comment( o.author, o.permlink );
   const auto& voter   = _db.get_account( o.voter );

   FC_ASSERT( voter.can_vote, "Voter has declined their voting rights." );

   if( o.weight > 0 ) FC_ASSERT( comment.allow_votes, "Votes are not allowed on the comment." );

   if( _db.calculate_discussion_payout_time( comment ) == fc::time_point_sec::maximum() )
   {
#ifndef CLEAR_VOTES
      const auto& comment_vote_idx = _db.get_index< comment_vote_index >().indices().get< by_comment_voter >();
      auto itr = comment_vote_idx.find( std::make_tuple( comment.id, voter.id ) );

      if( itr == comment_vote_idx.end() )
         _db.create< comment_vote_object >( [&]( comment_vote_object& cvo )
         {
            cvo.voter = voter.id;
            cvo.comment = comment.id;
            cvo.vote_percent = o.weight;
            cvo.last_update = _db.head_block_time();
         });
      else
         _db.modify( *itr, [&]( comment_vote_object& cvo )
         {
            cvo.vote_percent = o.weight;
            cvo.last_update = _db.head_block_time();
         });
#endif
      return;
   }

   const auto& comment_vote_idx = _db.get_index< comment_vote_index >().indices().get< by_comment_voter >();
   auto itr = comment_vote_idx.find( std::make_tuple( comment.id, voter.id ) );

   int64_t elapsed_seconds   = (_db.head_block_time() - voter.last_vote_time).to_seconds();

   FC_ASSERT( elapsed_seconds >= GAMEBANK_MIN_VOTE_INTERVAL_SEC, "Can only vote once every 3 seconds." );

   int64_t regenerated_power = (GAMEBANK_100_PERCENT * elapsed_seconds) / GAMEBANK_VOTE_REGENERATION_SECONDS;
   int64_t current_power     = std::min( int64_t(voter.voting_power + regenerated_power), int64_t(GAMEBANK_100_PERCENT) );
   FC_ASSERT( current_power > 0, "Account currently does not have voting power." );

   int64_t  abs_weight    = abs(o.weight);
   // Less rounding error would occur if we did the division last, but we need to maintain backward
   // compatibility with the previous implementation which was replaced in #1285
   int64_t  used_power  = ((current_power * abs_weight) / GAMEBANK_100_PERCENT) * (60*60*24);

   const dynamic_global_property_object& dgpo = _db.get_dynamic_global_properties();

   // The second multiplication is rounded up as of HF 259
   int64_t max_vote_denom = dgpo.vote_power_reserve_rate * GAMEBANK_VOTE_REGENERATION_SECONDS;
   FC_ASSERT( max_vote_denom > 0 );


   used_power = (used_power + max_vote_denom - 1) / max_vote_denom;
   
   FC_ASSERT( used_power <= current_power, "Account does not have enough power to vote." );

   int64_t abs_rshares    = ((uint128_t( _db.get_effective_vesting_shares( voter, GBS_SYMBOL ).amount.value ) * used_power) / (GAMEBANK_100_PERCENT)).to_uint64();
   

   if( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) )
   {
      abs_rshares -= GAMEBANK_VOTE_DUST_THRESHOLD;
      abs_rshares = std::max( int64_t(0), abs_rshares );
   }
   else
   {
      FC_ASSERT( abs_rshares > GAMEBANK_VOTE_DUST_THRESHOLD || o.weight == 0, "Voting weight is too small, please accumulate more voting power or gamebank power." );
   }



   // Lazily delete vote
   if( itr != comment_vote_idx.end() && itr->num_changes == -1 )
   {
      FC_ASSERT( false, "Cannot vote again on a comment after payout." );

      _db.remove( *itr );
      itr = comment_vote_idx.end();
   }

   if( itr == comment_vote_idx.end() )
   {
      FC_ASSERT( o.weight != 0, "Vote weight cannot be 0." );
      /// this is the rshares voting for or against the post
      int64_t rshares        = o.weight < 0 ? -abs_rshares : abs_rshares;

      if( rshares > 0 )
      {
         FC_ASSERT( _db.head_block_time() < comment.cashout_time - GAMEBANK_UPVOTE_LOCKOUT_TIME, "Cannot increase payout within last twelve hours before payout." );
      }

      //used_power /= (50*7); /// a 100% vote means use .28% of voting power which should force users to spread their votes around over 50+ posts day for a week
      //if( used_power == 0 ) used_power = 1;

      _db.modify( voter, [&]( account_object& a ){
         a.voting_power = current_power - used_power;
         a.last_vote_time = _db.head_block_time();
      });

      /// if the current net_rshares is less than 0, the post is getting 0 rewards so it is not factored into total rshares^2
      fc::uint128_t old_rshares = std::max(comment.net_rshares.value, int64_t(0));
      const auto& root = _db.get( comment.root_comment );
     // auto old_root_abs_rshares = root.children_abs_rshares.value;

      fc::uint128_t avg_cashout_sec;

      

      FC_ASSERT( abs_rshares > 0, "Cannot vote with 0 rshares." );

      auto old_vote_rshares = comment.vote_rshares;

      _db.modify( comment, [&]( comment_object& c ){
         c.net_rshares += rshares;
         c.abs_rshares += abs_rshares;
         if( rshares > 0 )
            c.vote_rshares += rshares;
         if( rshares > 0 )
            c.net_votes++;
         else
            c.net_votes--;

      });

      _db.modify( root, [&]( comment_object& c )
      {
         c.children_abs_rshares += abs_rshares;    
      });

      fc::uint128_t new_rshares = std::max( comment.net_rshares.value, int64_t(0));

      /// calculate rshares2 value
      new_rshares = util::evaluate_reward_curve( new_rshares );
      old_rshares = util::evaluate_reward_curve( old_rshares );

      uint64_t max_vote_weight = 0;

      /** this verifies uniqueness of voter
       *
       *  cv.weight / c.total_vote_weight ==> % of rshares increase that is accounted for by the vote
       *
       *  W(R) = B * R / ( R + 2S )
       *  W(R) is bounded above by B. B is fixed at 2^64 - 1, so all weights fit in a 64 bit integer.
       *
       *  The equation for an individual vote is:
       *    W(R_N) - W(R_N-1), which is the delta increase of proportional weight
       *
       *  c.total_vote_weight =
       *    W(R_1) - W(R_0) +
       *    W(R_2) - W(R_1) + ...
       *    W(R_N) - W(R_N-1) = W(R_N) - W(R_0)
       *
       *  Since W(R_0) = 0, c.total_vote_weight is also bounded above by B and will always fit in a 64 bit integer.
       *
      **/
      _db.create<comment_vote_object>( [&]( comment_vote_object& cv ){
         cv.voter   = voter.id;
         cv.comment = comment.id;
         cv.rshares = rshares;
         cv.vote_percent = o.weight;
         cv.last_update = _db.head_block_time();

         bool curation_reward_eligible = rshares > 0 && (comment.last_payout == fc::time_point_sec()) && comment.allow_curation_rewards;

         if( curation_reward_eligible )
            curation_reward_eligible = _db.get_curation_rewards_percent( comment ) > 0;

         if( curation_reward_eligible )
         {
            if( comment.created < fc::time_point_sec(GAMEBANK_HARDFORK_0_6_REVERSE_AUCTION_TIME) ) {
               u512 rshares3(rshares);
               u256 total2( comment.abs_rshares.value );

               rshares3 = rshares3 * rshares3 * rshares3;

               total2 *= total2;
               cv.weight = static_cast<uint64_t>( rshares3 / total2 );
            } else {// cv.weight = W(R_1) - W(R_0)
           //    const uint128_t two_s = 2 * util::get_content_constant_s();

                  const auto& reward_fund = _db.get_reward_fund( comment );
                  auto curve = reward_fund.curation_reward_curve;
                  uint64_t old_weight = util::evaluate_reward_curve( old_vote_rshares.value, curve, reward_fund.content_constant ).to_uint64();
                  uint64_t new_weight = util::evaluate_reward_curve( comment.vote_rshares.value, curve, reward_fund.content_constant ).to_uint64();
                  cv.weight = new_weight - old_weight;
               

               
            }

            max_vote_weight = cv.weight;

            if( _db.head_block_time() > fc::time_point_sec(GAMEBANK_HARDFORK_0_6_REVERSE_AUCTION_TIME) )  /// start enforcing this prior to the hardfork
            {
               /// discount weight by time
               uint128_t w(max_vote_weight);
               uint64_t delta_t = std::min( uint64_t((cv.last_update - comment.created).to_seconds()), uint64_t(GAMEBANK_REVERSE_AUCTION_WINDOW_SECONDS) );

               w *= delta_t;
               w /= GAMEBANK_REVERSE_AUCTION_WINDOW_SECONDS;
               cv.weight = w.to_uint64();
            }
         }
         else
         {
            cv.weight = 0;
         }
      });

      if( max_vote_weight ) // Optimization
      {
         _db.modify( comment, [&]( comment_object& c )
         {
            c.total_vote_weight += max_vote_weight;
         });
      }

   }
   else
   {
      FC_ASSERT( itr->num_changes < GAMEBANK_MAX_VOTE_CHANGES, "Voter has used the maximum number of vote changes on this comment." );

      FC_ASSERT( itr->vote_percent != o.weight, "You have already voted in a similar way." );

      /// this is the rshares voting for or against the post
      int64_t rshares        = o.weight < 0 ? -abs_rshares : abs_rshares;

      if( itr->rshares < rshares )
      {
         FC_ASSERT( _db.head_block_time() < comment.cashout_time - GAMEBANK_UPVOTE_LOCKOUT_TIME, "Cannot increase payout within last twelve hours before payout." );
         
      }

      _db.modify( voter, [&]( account_object& a ){
         a.voting_power = current_power - used_power;
         a.last_vote_time = _db.head_block_time();
      });

      /// if the current net_rshares is less than 0, the post is getting 0 rewards so it is not factored into total rshares^2
      fc::uint128_t old_rshares = std::max(comment.net_rshares.value, int64_t(0));
      const auto& root = _db.get( comment.root_comment );
      //auto old_root_abs_rshares = root.children_abs_rshares.value;

      fc::uint128_t avg_cashout_sec;

      

      _db.modify( comment, [&]( comment_object& c )
      {
         c.net_rshares -= itr->rshares;
         c.net_rshares += rshares;
         c.abs_rshares += abs_rshares;

         /// TODO: figure out how to handle remove a vote (rshares == 0 )
         if( rshares > 0 && itr->rshares < 0 )
            c.net_votes += 2;
         else if( rshares > 0 && itr->rshares == 0 )
            c.net_votes += 1;
         else if( rshares == 0 && itr->rshares < 0 )
            c.net_votes += 1;
         else if( rshares == 0 && itr->rshares > 0 )
            c.net_votes -= 1;
         else if( rshares < 0 && itr->rshares == 0 )
            c.net_votes -= 1;
         else if( rshares < 0 && itr->rshares > 0 )
            c.net_votes -= 2;
      });

      _db.modify( root, [&]( comment_object& c )
      {
         c.children_abs_rshares += abs_rshares;

       
         
      });

      fc::uint128_t new_rshares = std::max( comment.net_rshares.value, int64_t(0));

      /// calculate rshares2 value
      new_rshares = util::evaluate_reward_curve( new_rshares );
      old_rshares = util::evaluate_reward_curve( old_rshares );


      _db.modify( comment, [&]( comment_object& c )
      {
         c.total_vote_weight -= itr->weight;
      });

      _db.modify( *itr, [&]( comment_vote_object& cv )
      {
         cv.rshares = rshares;
         cv.vote_percent = o.weight;
         cv.last_update = _db.head_block_time();
         cv.weight = 0;
         cv.num_changes += 1;
      });

      
   }

} FC_CAPTURE_AND_RETHROW( (o)) }

void custom_evaluator::do_apply( const custom_operation& o )
{
   database& d = db();
   if( d.is_producing() )
      FC_ASSERT( o.data.size() <= 8192, "custom_operation must be less than 8k" );
}

void custom_json_evaluator::do_apply( const custom_json_operation& o )
{
   database& d = db();

   if( d.is_producing() )
      FC_ASSERT( o.json.length() <= 8192, "custom_json_operation json must be less than 8k" );

   std::shared_ptr< custom_operation_interpreter > eval = d.get_custom_json_evaluator( o.id );
   if( !eval )
      return;

   try
   {
      eval->apply( o );
   }
   catch( const fc::exception& e )
   {
      if( d.is_producing() )
         throw e;
   }
   catch(...)
   {
      elog( "Unexpected exception applying custom json evaluator." );
   }
}


void custom_binary_evaluator::do_apply( const custom_binary_operation& o )
{
   database& d = db();
   if( d.is_producing() )
   {
      FC_ASSERT( o.data.size() <= 8192, "custom_binary_operation data must be less than 8k" );
      FC_ASSERT( false, "custom_binary_operation is deprecated" );
   }
  

   std::shared_ptr< custom_operation_interpreter > eval = d.get_custom_json_evaluator( o.id );
   if( !eval )
      return;

   try
   {
      eval->apply( o );
   }
   catch( const fc::exception& e )
   {
      if( d.is_producing() )
         throw e;
   }
   catch(...)
   {
      elog( "Unexpected exception applying custom json evaluator." );
   }
}


template<typename Operation>
void pow_apply( database& db, Operation o )
{
   const auto& dgp = db.get_dynamic_global_properties();


      const auto& witness_by_work = db.get_index<witness_index>().indices().get<by_work>();
      auto work_itr = witness_by_work.find( o.work.work );
      if( work_itr != witness_by_work.end() )
      {
          FC_ASSERT( !"DUPLICATE WORK DISCOVERED", "${w}  ${witness}",("w",o)("wit",*work_itr) );
      }


   const auto& accounts_by_name = db.get_index<account_index>().indices().get<by_name>();

   auto itr = accounts_by_name.find(o.get_worker_account());
   if(itr == accounts_by_name.end())
   {
      db.create< account_object >( [&]( account_object& acc )
      {
         initialize_account_object( acc, o.get_worker_account(), o.work.worker, dgp, true /*mined*/, account_name_type(), db.get_hardfork() );
         // ^ empty recovery account parameter means highest voted witness at time of recovery
      });

      db.create< account_authority_object >( [&]( account_authority_object& auth )
      {
         auth.account = o.get_worker_account();
         auth.owner = authority( 1, o.work.worker, 1);
         auth.active = auth.owner;
         auth.posting = auth.owner;
      });
   }

   const auto& worker_account = db.get_account( o.get_worker_account() ); // verify it exists
   const auto& worker_auth = db.get< account_authority_object, by_account >( o.get_worker_account() );
   FC_ASSERT( worker_auth.active.num_auths() == 1, "Miners can only have one key authority. ${a}", ("a",worker_auth.active) );
   FC_ASSERT( worker_auth.active.key_auths.size() == 1, "Miners may only have one key authority." );
   FC_ASSERT( worker_auth.active.key_auths.begin()->first == o.work.worker, "Work must be performed by key that signed the work." );
   FC_ASSERT( o.block_id == db.head_block_id(), "pow not for last block" );
   
   FC_ASSERT( worker_account.last_account_update < db.head_block_time(), "Worker account must not have updated their account this block." );

   fc::sha256 target = db.get_pow_target();

   FC_ASSERT( o.work.work < target, "Work lacks sufficient difficulty." );

   db.modify( dgp, [&]( dynamic_global_property_object& p )
   {
      p.total_pow++; // make sure this doesn't break anything...
      p.num_pow_witnesses++;
   });


   const witness_object* cur_witness = db.find_witness( worker_account.name );
   if( cur_witness ) {
      FC_ASSERT( cur_witness->pow_worker == 0, "This account is already scheduled for pow block production." );
      db.modify(*cur_witness, [&]( witness_object& w ){
          copy_legacy_chain_properties< true >( w.props, o.props );
          w.pow_worker        = dgp.total_pow;
          w.last_work         = o.work.work;
      });
   } else {
      db.create<witness_object>( [&]( witness_object& w )
      {
          w.owner             = o.get_worker_account();
          copy_legacy_chain_properties< true >( w.props, o.props );
          w.signing_key       = o.work.worker;
          w.pow_worker        = dgp.total_pow;
          w.last_work         = o.work.work;
      });
   }
   /// POW reward depends upon whether we are before or after MINER_VOTING kicks in
   asset pow_reward = db.get_pow_reward();
   if( db.head_block_num() < GAMEBANK_START_MINER_VOTING_BLOCK )
      pow_reward.amount *= GAMEBANK_MAX_WITNESSES;
   db.adjust_supply( pow_reward, true );

   /// pay the witness that includes this POW
   const auto& inc_witness = db.get_account( dgp.current_witness );
   if( db.head_block_num() < GAMEBANK_START_MINER_VOTING_BLOCK )
      db.adjust_balance( inc_witness, pow_reward );
   else
      db.create_vesting( inc_witness, pow_reward );
}

void pow_evaluator::do_apply( const pow_operation& o ) {
   FC_ASSERT( false, "pow is deprecated. Use pow2 instead" );
   pow_apply( db(), o );
}


void pow2_evaluator::do_apply( const pow2_operation& o )
{
   database& db = this->db();
   FC_ASSERT( false, "mining is now disabled" );

   const auto& dgp = db.get_dynamic_global_properties();
   uint32_t target_pow = db.get_pow_summary_target();
   account_name_type worker_account;


      const auto& work = o.work.get< equihash_pow >();
      FC_ASSERT( work.prev_block == db.head_block_id(), "Equihash pow op not for last block" );
      auto recent_block_num = protocol::block_header::num_from_id( work.input.prev_block );
      FC_ASSERT( recent_block_num > dgp.last_irreversible_block_num,
         "Equihash pow done for block older than last irreversible block num" );
      FC_ASSERT( work.pow_summary < target_pow, "Insufficient work difficulty. Work: ${w}, Target: ${t}", ("w",work.pow_summary)("t", target_pow) );
      worker_account = work.input.worker_account;
   

   

   FC_ASSERT( o.props.maximum_block_size >= GAMEBANK_MIN_BLOCK_SIZE_LIMIT * 2, "Voted maximum block size is too small." );

   db.modify( dgp, [&]( dynamic_global_property_object& p )
   {
      p.total_pow++;
      p.num_pow_witnesses++;
   });

   const auto& accounts_by_name = db.get_index<account_index>().indices().get<by_name>();
   auto itr = accounts_by_name.find( worker_account );
   if(itr == accounts_by_name.end())
   {
      FC_ASSERT( o.new_owner_key.valid(), "New owner key is not valid." );
      db.create< account_object >( [&]( account_object& acc )
      {
         initialize_account_object( acc, worker_account, *o.new_owner_key, dgp, true /*mined*/, account_name_type(), _db.get_hardfork() );
         // ^ empty recovery account parameter means highest voted witness at time of recovery
      });

      db.create< account_authority_object >( [&]( account_authority_object& auth )
      {
         auth.account = worker_account;
         auth.owner = authority( 1, *o.new_owner_key, 1);
         auth.active = auth.owner;
         auth.posting = auth.owner;
      });

      db.create<witness_object>( [&]( witness_object& w )
      {
          w.owner             = worker_account;
          copy_legacy_chain_properties< true >( w.props, o.props );
          w.signing_key       = *o.new_owner_key;
          w.pow_worker        = dgp.total_pow;
      });
   }
   else
   {
      FC_ASSERT( !o.new_owner_key.valid(), "Cannot specify an owner key unless creating account." );
      const witness_object* cur_witness = db.find_witness( worker_account );
      FC_ASSERT( cur_witness, "Witness must be created for existing account before mining.");
      FC_ASSERT( cur_witness->pow_worker == 0, "This account is already scheduled for pow block production." );
      db.modify(*cur_witness, [&]( witness_object& w )
      {
          copy_legacy_chain_properties< true >( w.props, o.props );
          w.pow_worker        = dgp.total_pow;
      });
   }

}

void feed_publish_evaluator::do_apply( const feed_publish_operation& o )
{
   if( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) )
      FC_ASSERT( is_asset_type( o.exchange_rate.base, GBD_SYMBOL ) && is_asset_type( o.exchange_rate.quote, GBC_SYMBOL ),
            "Price feed must be a GBD/GBC price" );

   const auto& witness = _db.get_witness( o.publisher );
   _db.modify( witness, [&]( witness_object& w )
   {
      w.gbd_exchange_rate = o.exchange_rate;
      w.last_gbd_exchange_update = _db.head_block_time();
   });
}

void convert_evaluator::do_apply( const convert_operation& o )
{
  FC_ASSERT( _db.get_balance( o.owner, o.amount.symbol ) >= o.amount, "Account does not have sufficient balance for conversion." );

  _db.adjust_balance( o.owner, -o.amount );

  const auto& fhistory = _db.get_feed_history();
  FC_ASSERT( !fhistory.current_median_history.is_null(), "Cannot convert GBD because there is no price feed." );

  auto gamebank_conversion_delay = fc::days(7);
  
  gamebank_conversion_delay = GAMEBANK_CONVERSION_DELAY;

  _db.create<convert_request_object>( [&]( convert_request_object& obj )
  {
      obj.owner           = o.owner;
      obj.requestid       = o.requestid;
      obj.amount          = o.amount;
      obj.conversion_date = _db.head_block_time() + gamebank_conversion_delay;
  });

}

void limit_order_create_evaluator::do_apply( const limit_order_create_operation& o )
{
   FC_ASSERT( o.expiration > _db.head_block_time(), "Limit order has to expire after head block time." );

   if( _db.has_hardfork( GAMEBANK_HARDFORK_0_1) )
   {
      FC_ASSERT( o.expiration <= _db.head_block_time() + GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION, "Limit Order Expiration must not be more than 28 days in the future" );
   }

   FC_ASSERT( _db.get_balance( o.owner, o.amount_to_sell.symbol ) >= o.amount_to_sell, "Account does not have sufficient funds for limit order." );

   _db.adjust_balance( o.owner, -o.amount_to_sell );

   const auto& order = _db.create<limit_order_object>( [&]( limit_order_object& obj )
   {
       obj.created    = _db.head_block_time();
       obj.seller     = o.owner;
       obj.orderid    = o.orderid;
       obj.for_sale   = o.amount_to_sell.amount;
       obj.sell_price = o.get_price();

       FC_TODO( "Check past order expirations and cleanup after HF 20" )
       if( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) )
       {
          obj.expiration = o.expiration;
       }
       else
       {
          uint32_t rand_offset = _db.head_block_id()._hash[4] % 86400;
          obj.expiration = std::min( o.expiration, fc::time_point_sec( GAMEBANK_HARDFORK_0_1_TIME + GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION + rand_offset ) );
       }
   });

   bool filled = _db.apply_order( order );

   if( o.fill_or_kill ) FC_ASSERT( filled, "Cancelling order because it was not filled." );
}

void limit_order_create2_evaluator::do_apply( const limit_order_create2_operation& o )
{
   FC_ASSERT( o.expiration > _db.head_block_time(), "Limit order has to expire after head block time." );

   if( _db.has_hardfork( GAMEBANK_HARDFORK_0_1) )
   {
      FC_ASSERT( o.expiration <= _db.head_block_time() + GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION, "Limit Order Expiration must not be more than 28 days in the future" );
   }

   FC_ASSERT( _db.get_balance( o.owner, o.amount_to_sell.symbol ) >= o.amount_to_sell, "Account does not have sufficient funds for limit order." );

   _db.adjust_balance( o.owner, -o.amount_to_sell );

   const auto& order = _db.create<limit_order_object>( [&]( limit_order_object& obj )
   {
       obj.created    = _db.head_block_time();
       obj.seller     = o.owner;
       obj.orderid    = o.orderid;
       obj.for_sale   = o.amount_to_sell.amount;
       obj.sell_price = o.exchange_rate;

       FC_TODO( "Check past order expirations and cleanup after HF 20" )
       if( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) )
       {
          obj.expiration = o.expiration;
       }
       else
       {
          obj.expiration = std::min( o.expiration, fc::time_point_sec( GAMEBANK_HARDFORK_0_1_TIME + GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION ) );
       }
   });

   bool filled = _db.apply_order( order );

   if( o.fill_or_kill ) FC_ASSERT( filled, "Cancelling order because it was not filled." );
}

void limit_order_cancel_evaluator::do_apply( const limit_order_cancel_operation& o )
{
   _db.cancel_order( _db.get_limit_order( o.owner, o.orderid ) );
}

void report_over_production_evaluator::do_apply( const report_over_production_operation& o )
{
   FC_ASSERT( false, "report_over_production_operation is disabled." );
}

void claim_account_evaluator::do_apply( const claim_account_operation& o )
{
   FC_ASSERT( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ), "claim_account_operation is not enabled until hardfork 20." );

   const auto& creator = _db.get_account( o.creator );
   const auto& wso = _db.get_witness_schedule_object();

   FC_ASSERT( creator.balance >= o.fee, "Insufficient balance to create account.", ( "creator.balance", creator.balance )( "required", o.fee ) );

   FC_ASSERT( o.fee >= wso.median_props.account_creation_fee, "Insufficient Fee: ${f} required, ${p} provided.",
               ("f", wso.median_props.account_creation_fee)
               ("p", o.fee) );

   _db.adjust_balance( _db.get_account( GAMEBANK_NULL_ACCOUNT ), o.fee );

   _db.modify( creator, [&]( account_object& a )
   {
      a.balance -= o.fee;
      a.pending_claimed_accounts++;
   });
}

void create_claimed_account_evaluator::do_apply( const create_claimed_account_operation& o )
{
   FC_ASSERT( _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ), "create_claimed_account_operation is not enabled until hardfork 20." );

   const auto& creator = _db.get_account( o.creator );
   const auto& props = _db.get_dynamic_global_properties();

   FC_ASSERT( creator.pending_claimed_accounts > 0, "${creator} has no claimed accounts to create", ( "creator", o.creator ) );

   verify_authority_accounts_exist( _db, o.owner, o.new_account_name, authority::owner );
   verify_authority_accounts_exist( _db, o.active, o.new_account_name, authority::active );
   verify_authority_accounts_exist( _db, o.posting, o.new_account_name, authority::posting );

   _db.modify( creator, [&]( account_object& a )
   {
      a.pending_claimed_accounts--;
   });

   _db.create< account_object >( [&]( account_object& acc )
   {
      initialize_account_object( acc, o.new_account_name, o.memo_key, props, false /*mined*/, o.creator, _db.get_hardfork() );
      #ifndef IS_LOW_MEM
         from_string( acc.json_metadata, o.json_metadata );
      #endif
   });

   _db.create< account_authority_object >( [&]( account_authority_object& auth )
   {
      auth.account = o.new_account_name;
      auth.owner = o.owner;
      auth.active = o.active;
      auth.posting = o.posting;
      auth.last_owner_update = fc::time_point_sec::min();
   });

}

void request_account_recovery_evaluator::do_apply( const request_account_recovery_operation& o )
{
   const auto& account_to_recover = _db.get_account( o.account_to_recover );

   if ( account_to_recover.recovery_account.length() )   // Make sure recovery matches expected recovery account
   {
      FC_ASSERT( account_to_recover.recovery_account == o.recovery_account, "Cannot recover an account that does not have you as there recovery partner." );
      if( o.recovery_account == GAMEBANK_TEMP_ACCOUNT )
         wlog( "Recovery by temp account" );
   }
   else                                                  // Empty string recovery account defaults to top witness
      FC_ASSERT( _db.get_index< witness_index >().indices().get< by_vote_name >().begin()->owner == o.recovery_account, "Top witness must recover an account with no recovery partner." );

   const auto& recovery_request_idx = _db.get_index< account_recovery_request_index >().indices().get< by_account >();
   auto request = recovery_request_idx.find( o.account_to_recover );

   if( request == recovery_request_idx.end() ) // New Request
   {
      FC_ASSERT( !o.new_owner_authority.is_impossible(), "Cannot recover using an impossible authority." );
      FC_ASSERT( o.new_owner_authority.weight_threshold, "Cannot recover using an open authority." );

      // Check accounts in the new authority exist
    
         for( auto& a : o.new_owner_authority.account_auths )
         {
            _db.get_account( a.first );
         }
      

      _db.create< account_recovery_request_object >( [&]( account_recovery_request_object& req )
      {
         req.account_to_recover = o.account_to_recover;
         req.new_owner_authority = o.new_owner_authority;
         req.expires = _db.head_block_time() + GAMEBANK_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
      });
   }
   else if( o.new_owner_authority.weight_threshold == 0 ) // Cancel Request if authority is open
   {
      _db.remove( *request );
   }
   else // Change Request
   {
      FC_ASSERT( !o.new_owner_authority.is_impossible(), "Cannot recover using an impossible authority." );

      // Check accounts in the new authority exist
      
         for( auto& a : o.new_owner_authority.account_auths )
         {
            _db.get_account( a.first );
         }
      

      _db.modify( *request, [&]( account_recovery_request_object& req )
      {
         req.new_owner_authority = o.new_owner_authority;
         req.expires = _db.head_block_time() + GAMEBANK_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
      });
   }
}

void recover_account_evaluator::do_apply( const recover_account_operation& o )
{
   const auto& account = _db.get_account( o.account_to_recover );

   
   FC_ASSERT( _db.head_block_time() - account.last_account_recovery > GAMEBANK_OWNER_UPDATE_LIMIT, "Owner authority can only be updated once an hour." );

   const auto& recovery_request_idx = _db.get_index< account_recovery_request_index >().indices().get< by_account >();
   auto request = recovery_request_idx.find( o.account_to_recover );

   FC_ASSERT( request != recovery_request_idx.end(), "There are no active recovery requests for this account." );
   FC_ASSERT( request->new_owner_authority == o.new_owner_authority, "New owner authority does not match recovery request." );

   const auto& recent_auth_idx = _db.get_index< owner_authority_history_index >().indices().get< by_account >();
   auto hist = recent_auth_idx.lower_bound( o.account_to_recover );
   bool found = false;

   while( hist != recent_auth_idx.end() && hist->account == o.account_to_recover && !found )
   {
      found = hist->previous_owner_authority == o.recent_owner_authority;
      if( found ) break;
      ++hist;
   }

   FC_ASSERT( found, "Recent authority not found in authority history." );

   _db.remove( *request ); // Remove first, update_owner_authority may invalidate iterator
   _db.update_owner_authority( account, o.new_owner_authority );
   _db.modify( account, [&]( account_object& a )
   {
      a.last_account_recovery = _db.head_block_time();
   });
}

void change_recovery_account_evaluator::do_apply( const change_recovery_account_operation& o )
{
   _db.get_account( o.new_recovery_account ); // Simply validate account exists
   const auto& account_to_recover = _db.get_account( o.account_to_recover );

   const auto& change_recovery_idx = _db.get_index< change_recovery_account_request_index >().indices().get< by_account >();
   auto request = change_recovery_idx.find( o.account_to_recover );

   if( request == change_recovery_idx.end() ) // New request
   {
      _db.create< change_recovery_account_request_object >( [&]( change_recovery_account_request_object& req )
      {
         req.account_to_recover = o.account_to_recover;
         req.recovery_account = o.new_recovery_account;
         req.effective_on = _db.head_block_time() + GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD;
      });
   }
   else if( account_to_recover.recovery_account != o.new_recovery_account ) // Change existing request
   {
      _db.modify( *request, [&]( change_recovery_account_request_object& req )
      {
         req.recovery_account = o.new_recovery_account;
         req.effective_on = _db.head_block_time() + GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD;
      });
   }
   else // Request exists and changing back to current recovery account
   {
      _db.remove( *request );
   }
}

void transfer_to_savings_evaluator::do_apply( const transfer_to_savings_operation& op )
{
   const auto& from = _db.get_account( op.from );
   const auto& to   = _db.get_account(op.to);
   FC_ASSERT( _db.get_balance( from, op.amount.symbol ) >= op.amount, "Account does not have sufficient funds to transfer to savings." );

   _db.adjust_balance( from, -op.amount );
   _db.adjust_savings_balance( to, op.amount );
}

void transfer_from_savings_evaluator::do_apply( const transfer_from_savings_operation& op )
{
   const auto& from = _db.get_account( op.from );
   _db.get_account(op.to); // Verify to account exists

   FC_ASSERT( from.savings_withdraw_requests < GAMEBANK_SAVINGS_WITHDRAW_REQUEST_LIMIT, "Account has reached limit for pending withdraw requests." );

   FC_ASSERT( _db.get_savings_balance( from, op.amount.symbol ) >= op.amount );
   _db.adjust_savings_balance( from, -op.amount );
   _db.create<savings_withdraw_object>( [&]( savings_withdraw_object& s ) {
      s.from   = op.from;
      s.to     = op.to;
      s.amount = op.amount;
#ifndef IS_LOW_MEM
      from_string( s.memo, op.memo );
#endif
      s.request_id = op.request_id;
      s.complete = _db.head_block_time() + GAMEBANK_SAVINGS_WITHDRAW_TIME;
   });

   _db.modify( from, [&]( account_object& a )
   {
      a.savings_withdraw_requests++;
   });
}

void cancel_transfer_from_savings_evaluator::do_apply( const cancel_transfer_from_savings_operation& op )
{
   const auto& swo = _db.get_savings_withdraw( op.from, op.request_id );
   _db.adjust_savings_balance( _db.get_account( swo.from ), swo.amount );
   _db.remove( swo );

   const auto& from = _db.get_account( op.from );
   _db.modify( from, [&]( account_object& a )
   {
      a.savings_withdraw_requests--;
   });
}

void decline_voting_rights_evaluator::do_apply( const decline_voting_rights_operation& o )
{
   

   const auto& account = _db.get_account( o.account );
   const auto& request_idx = _db.get_index< decline_voting_rights_request_index >().indices().get< by_account >();
   auto itr = request_idx.find( account.name );

   if( o.decline )
   {
      FC_ASSERT( itr == request_idx.end(), "Cannot create new request because one already exists." );

      _db.create< decline_voting_rights_request_object >( [&]( decline_voting_rights_request_object& req )
      {
         req.account = account.name;
         req.effective_date = _db.head_block_time() + GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD;
      });
   }
   else
   {
      FC_ASSERT( itr != request_idx.end(), "Cannot cancel the request because it does not exist." );
      _db.remove( *itr );
   }
}

void reset_account_evaluator::do_apply( const reset_account_operation& op )
{
   FC_ASSERT( false, "Reset Account Operation is currently disabled." );
/*
   const auto& acnt = _db.get_account( op.account_to_reset );
   auto band = _db.find< account_bandwidth_object, by_account_bandwidth_type >( boost::make_tuple( op.account_to_reset, bandwidth_type::old_forum ) );
   if( band != nullptr )
      FC_ASSERT( ( _db.head_block_time() - band->last_bandwidth_update ) > fc::days(60), "Account must be inactive for 60 days to be eligible for reset" );
   FC_ASSERT( acnt.reset_account == op.reset_account, "Reset account does not match reset account on account." );

   _db.update_owner_authority( acnt, op.new_owner_authority );
*/
}

void set_reset_account_evaluator::do_apply( const set_reset_account_operation& op )
{
   FC_ASSERT( false, "Set Reset Account Operation is currently disabled." );
/*
   const auto& acnt = _db.get_account( op.account );
   _db.get_account( op.reset_account );

   FC_ASSERT( acnt.reset_account == op.current_reset_account, "Current reset account does not match reset account on account." );
   FC_ASSERT( acnt.reset_account != op.reset_account, "Reset account must change" );

   _db.modify( acnt, [&]( account_object& a )
   {
       a.reset_account = op.reset_account;
   });
*/
}

void claim_reward_balance_evaluator::do_apply( const claim_reward_balance_operation& op )
{
   const auto& acnt = _db.get_account( op.account );

   FC_ASSERT( op.reward_gbc <= acnt.reward_gbc_balance, "Cannot claim that much GBC. Claim: ${c} Actual: ${a}",
      ("c", op.reward_gbc)("a", acnt.reward_gbc_balance) );
   FC_ASSERT( op.reward_gbd <= acnt.reward_gbd_balance, "Cannot claim that much GBD. Claim: ${c} Actual: ${a}",
      ("c", op.reward_gbd)("a", acnt.reward_gbd_balance) );
   FC_ASSERT( op.reward_vests <= acnt.reward_vesting_balance, "Cannot claim that much GBS. Claim: ${c} Actual: ${a}",
      ("c", op.reward_vests)("a", acnt.reward_vesting_balance) );

   asset reward_vesting_gbc_to_move = asset( 0, GBC_SYMBOL );
   if( op.reward_vests == acnt.reward_vesting_balance )
      reward_vesting_gbc_to_move = acnt.reward_vesting_gbc;
   else
      reward_vesting_gbc_to_move = asset( ( ( uint128_t( op.reward_vests.amount.value ) * uint128_t( acnt.reward_vesting_gbc.amount.value ) )
         / uint128_t( acnt.reward_vesting_balance.amount.value ) ).to_uint64(), GBC_SYMBOL );

   _db.adjust_reward_balance( acnt, -op.reward_gbc );
   _db.adjust_reward_balance( acnt, -op.reward_gbd );
   _db.adjust_balance( acnt, op.reward_gbc );
   _db.adjust_balance( acnt, op.reward_gbd );

   _db.modify( acnt, [&]( account_object& a )
   {
      a.vesting_shares += op.reward_vests;
      a.reward_vesting_balance -= op.reward_vests;
      a.reward_vesting_gbc -= reward_vesting_gbc_to_move;
   });

   _db.modify( _db.get_dynamic_global_properties(), [&]( dynamic_global_property_object& gpo )
   {
      gpo.total_vesting_shares += op.reward_vests;
      gpo.total_vesting_fund_gbc += reward_vesting_gbc_to_move;

      gpo.pending_rewarded_vesting_shares -= op.reward_vests;
      gpo.pending_rewarded_vesting_gbc -= reward_vesting_gbc_to_move;
   });

   _db.adjust_proxied_witness_votes( acnt, op.reward_vests.amount );
}

void delegate_vesting_shares_evaluator::do_apply( const delegate_vesting_shares_operation& op )
{
   const auto& delegator = _db.get_account( op.delegator );
   const auto& delegatee = _db.get_account( op.delegatee );
   auto delegation = _db.find< vesting_delegation_object, by_delegation >( boost::make_tuple( op.delegator, op.delegatee ) );

   auto available_shares = delegator.vesting_shares - delegator.delegated_vesting_shares - asset( delegator.to_withdraw - delegator.withdrawn, GBS_SYMBOL );

   const auto& wso = _db.get_witness_schedule_object();
   const auto& gpo = _db.get_dynamic_global_properties();

   // HF 20 increase fee meaning by 30x, reduce these thresholds to compensate.
   auto min_delegation = _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) ?
      asset( wso.median_props.account_creation_fee.amount / 3, GBC_SYMBOL ) * gpo.get_vesting_share_price() :
      asset( wso.median_props.account_creation_fee.amount * 10, GBC_SYMBOL ) * gpo.get_vesting_share_price();
   auto min_update = _db.has_hardfork( GAMEBANK_HARDFORK_0_1 ) ?
      asset( wso.median_props.account_creation_fee.amount / 30, GBC_SYMBOL ) * gpo.get_vesting_share_price() :
      wso.median_props.account_creation_fee * gpo.get_vesting_share_price();

   // If delegation doesn't exist, create it
   if( delegation == nullptr )
   {
      FC_ASSERT( available_shares >= op.vesting_shares, "Account does not have enough vesting shares to delegate." );
      FC_ASSERT( op.vesting_shares >= min_delegation, "Account must delegate a minimum of ${v}", ("v", min_delegation) );

      _db.create< vesting_delegation_object >( [&]( vesting_delegation_object& obj )
      {
         obj.delegator = op.delegator;
         obj.delegatee = op.delegatee;
         obj.vesting_shares = op.vesting_shares;
         obj.min_delegation_time = _db.head_block_time();
      });

      _db.modify( delegator, [&]( account_object& a )
      {
         a.delegated_vesting_shares += op.vesting_shares;
      });

      _db.modify( delegatee, [&]( account_object& a )
      {
         a.received_vesting_shares += op.vesting_shares;
      });
   }
   // Else if the delegation is increasing
   else if( op.vesting_shares >= delegation->vesting_shares )
   {
      auto delta = op.vesting_shares - delegation->vesting_shares;

      FC_ASSERT( delta >= min_update, "Gamebank Power increase is not enough of a difference. min_update: ${min}", ("min", min_update) );
      FC_ASSERT( available_shares >= op.vesting_shares - delegation->vesting_shares, "Account does not have enough vesting shares to delegate." );

      _db.modify( delegator, [&]( account_object& a )
      {
         a.delegated_vesting_shares += delta;
      });

      _db.modify( delegatee, [&]( account_object& a )
      {
         a.received_vesting_shares += delta;
      });

      _db.modify( *delegation, [&]( vesting_delegation_object& obj )
      {
         obj.vesting_shares = op.vesting_shares;
      });
   }
   // Else the delegation is decreasing
   else /* delegation->vesting_shares > op.vesting_shares */
   {
      auto delta = delegation->vesting_shares - op.vesting_shares;

      if( op.vesting_shares.amount > 0 )
      {
         FC_ASSERT( delta >= min_update, "Gamebank Power decrease is not enough of a difference. min_update: ${min}", ("min", min_update) );
         FC_ASSERT( op.vesting_shares >= min_delegation, "Delegation must be removed or leave minimum delegation amount of ${v}", ("v", min_delegation) );
      }
      else
      {
         FC_ASSERT( delegation->vesting_shares.amount > 0, "Delegation would set vesting_shares to zero, but it is already zero");
      }

      _db.create< vesting_delegation_expiration_object >( [&]( vesting_delegation_expiration_object& obj )
      {
         obj.delegator = op.delegator;
         obj.vesting_shares = delta;
         obj.expiration = std::max( _db.head_block_time() + gpo.delegation_return_period, delegation->min_delegation_time );
      });

      _db.modify( delegatee, [&]( account_object& a )
      {
         a.received_vesting_shares -= delta;
      });

      if( op.vesting_shares.amount > 0 )
      {
         _db.modify( *delegation, [&]( vesting_delegation_object& obj )
         {
            obj.vesting_shares = op.vesting_shares;
         });
      }
      else
      {
         _db.remove( *delegation );
      }
   }
}

void crowdfunding_evaluator::do_apply(const crowdfunding_operation& op)
{ try {
    FC_ASSERT(op.title.size() + op.body.size() + op.json_metadata.size(), "Crowdfunding content is empty.");
    FC_ASSERT(fc::is_utf8(op.json_metadata), "JSON Metadata must be UTF-8");

    const auto& originator = _db.get_account(op.originator);
    auto now = _db.head_block_time();

    FC_ASSERT(now > originator.last_crowdfunding_expire, "Crowdfunding is in progress.");

    const auto& by_permlink_idx = _db.get_index< crowdfunding_index >().indices().get< by_permlink >();
    auto itr = by_permlink_idx.find(boost::make_tuple(op.originator, op.permlink));

    FC_ASSERT(itr == by_permlink_idx.end(), "Permlink is exist.");

     auto expire = now + fc::days(3);

     _db.modify(originator, [&](account_object& a) {
         a.last_crowdfunding_expire = expire;
         a.crowdfunding_count++;
     });

     const auto& new_crowdfunding = _db.create< crowdfunding_object >([&](crowdfunding_object& co)
     {
         validate_permlink_0_1(op.permlink);

         co.originator = op.originator;
         from_string(co.permlink, op.permlink);

         co.created = now;
         co.expire = expire;
         co.last_raise = fc::time_point_sec::min();

         co.total_raise_value = op.raise;
     });

#ifndef IS_LOW_MEM
     _db.create< crowdfunding_content_object >([&](crowdfunding_content_object& coo)
     {
         coo.crowdfunding = new_crowdfunding.id;

         from_string(coo.title, op.title);
         if (op.body.size() < 1024 * 1024 * 128)
         {
             from_string(coo.body, op.body);
         }
         from_string(coo.json_metadata, op.json_metadata);
     });
#endif

} FC_CAPTURE_AND_RETHROW((op)) }


void invest_evaluator::do_apply(const invest_operation& o)
{ try {
    const auto& crowdfunding = _db.get_crowdfunding(o.originator, o.permlink);
    const auto& invester = _db.get_account(o.invester);
    auto now = _db.head_block_time();

    FC_ASSERT( now < crowdfunding.expire, "Crowdfunding is complete.");
    // 判断投资人钱是否够，投资总额是否超过上限
    FC_ASSERT( _db.get_balance(o.invester, o.raise.symbol) >= o.raise, "Invester does not have sufficient funds for invest." );
    FC_ASSERT( crowdfunding.curator_raise_value + o.raise <= crowdfunding.total_raise_value, "Invester does not have sufficient funds for invest." );

    // 投资人扣钱，众筹加钱
    _db.adjust_balance(o.invester, -o.raise);
    _db.modify(crowdfunding, [&](crowdfunding_object& co)
    {
        co.curator_raise_value += o.raise;
        co.last_raise = now;
    });

    const auto& crowdfunding_invest_idx = _db.get_index< crowdfunding_invest_index >().indices().get< by_crowdfunding_invester >();
    auto itr = crowdfunding_invest_idx.find(std::make_tuple(crowdfunding.id, invester.name));
    if (itr == crowdfunding_invest_idx.end())
    {
        _db.create< crowdfunding_invest_object >([&](crowdfunding_invest_object& cio)
        {
            cio.invester = invester.name;
            cio.crowdfunding = crowdfunding.id;
            cio.expire = crowdfunding.expire;
            cio.raise = o.raise;
            cio.last_update = now;
        });
    }
    else
    {
        _db.modify(*itr, [&](crowdfunding_invest_object& cio)
        {
            cio.raise += o.raise;
            cio.last_update = _db.head_block_time();
        });
    } 
} FC_CAPTURE_AND_RETHROW((o)) }

void nonfungible_fund_create_evaluator::do_apply(const nonfungible_fund_create_operation& op)
{
	try {
		const auto& creator = _db.get_account(op.creator);
		const auto& owner = _db.get_account(op.owner);
		auto now = _db.head_block_time();

		_db.create< nonfungible_fund_object >([&](nonfungible_fund_object& nfo)
		{
			nfo.creator = creator.name;
			nfo.owner = owner.name;
			from_string(nfo.meta_data, op.meta_data);
			nfo.created = now;
			nfo.last_update = now;
		});

	} FC_CAPTURE_AND_RETHROW((op))
}

void nonfungible_fund_transfer_evaluator::do_apply(const nonfungible_fund_transfer_operation& op)
{
	try {
		const auto& from = _db.get_account(op.from);
		const auto& to = _db.get_account(op.to);
		auto now = _db.head_block_time();

		const auto& nfo = _db.get< nonfungible_fund_object, by_id >(nonfungible_fund_id_type(op.fund_id));
		FC_ASSERT(nfo.owner == from.name, "from not own the fund");

        //check if the fund is on sale
        auto * on_sale_obj = _db.find< nonfungible_fund_on_sale_object, by_fund_id >(op.fund_id);
        FC_ASSERT(on_sale_obj == nullptr, "the fund is on sale");

		_db.modify(nfo, [&](nonfungible_fund_object& o)
		{
			o.owner = to.name;
			o.last_update = now;
		});

	} FC_CAPTURE_AND_RETHROW((op))
}

void nonfungible_fund_put_up_for_sale_evaluator::do_apply( const nonfungible_fund_put_up_for_sale_operation& op )
{
    FC_ASSERT( op.expiration > _db.head_block_time(), "Limit order has to expire after head block time." );
    
    // FC_ASSERT( o.expiration <= _db.head_block_time() + GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION, "Limit Order Expiration must not be more than 28 days in the future" );
    
    const auto& seller = _db.get_account(op.seller); 
    const auto& nfo = _db.get< nonfungible_fund_object, by_id >(nonfungible_fund_id_type(op.fund_id));
    
    FC_ASSERT(nfo.owner == seller.name, "seller does't own the fund");
    
    //check if the fund is on sale
    auto * on_sale_obj = _db.find< nonfungible_fund_on_sale_object, by_fund_id >(op.fund_id);
    FC_ASSERT(on_sale_obj == nullptr, "the fund is on sale");
    
     _db.create<nonfungible_fund_on_sale_object>( [&]( nonfungible_fund_on_sale_object& obj )
     {
         obj.fund_id        =  op.fund_id;
         obj.created        =  _db.head_block_time();
         obj.expiration     =  op.expiration;
         obj.seller         =  op.seller;
         obj.selling_price  =  op.get_price();     
     }); 
}
        
void nonfungible_fund_withdraw_from_sale_evaluator::do_apply( const nonfungible_fund_withdraw_from_sale_operation& op )
{
   try {
       const auto& seller = _db.get_account(op.seller); 
       const auto& nfo = _db.get< nonfungible_fund_object, by_id >(nonfungible_fund_id_type(op.fund_id));
    
       FC_ASSERT(nfo.owner == seller.name, "seller does't own the fund");
       
       const auto& on_sale_obj =      _db.get< nonfungible_fund_on_sale_object, by_fund_id >(op.fund_id);
     
       _db.remove(on_sale_obj);
   }  FC_CAPTURE_AND_RETHROW((op))
}

void nonfungible_fund_buy_evaluator::do_apply( const nonfungible_fund_buy_operation& op )
{
       try {
         const auto& nfo           =   _db.get< nonfungible_fund_object, by_id >(nonfungible_fund_id_type(op.fund_id));
         const auto& on_sale_obj   =   _db.get< nonfungible_fund_on_sale_object, by_fund_id >(op.fund_id);

         FC_ASSERT( _db.get_balance( op.buyer, on_sale_obj.selling_price.symbol ) >= on_sale_obj.selling_price, "Account does not have sufficient coins to buy the fund." );

         _db.adjust_balance( op.buyer,                       -on_sale_obj.selling_price );
         _db.adjust_balance( on_sale_obj.seller,              on_sale_obj.selling_price );

         _db.remove(on_sale_obj); 
 
         _db.modify(nfo, [&](nonfungible_fund_object& o)
         {
             o.owner = op.buyer;
             o.last_update = _db.head_block_time();
         });
  
       }  FC_CAPTURE_AND_RETHROW((op))
}


} } // gamebank::chain
