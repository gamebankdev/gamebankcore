#pragma once

#include <gamebank/chain/gamebank_object_types.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace gamebank { namespace plugins{ namespace witness {

using namespace std;
using namespace gamebank::chain;

#ifndef GAMEBANK_WITNESS_SPACE_ID
#define GAMEBANK_WITNESS_SPACE_ID 12
#endif

enum witness_plugin_object_type
{
   account_bandwidth_object_type = ( GAMEBANK_WITNESS_SPACE_ID << 8 ),
   content_edit_lock_object_type = ( GAMEBANK_WITNESS_SPACE_ID << 8 ) + 1,
   reserve_ratio_object_type     = ( GAMEBANK_WITNESS_SPACE_ID << 8 ) + 2,
};

enum bandwidth_type
{
   none,    ///
   forum,   ///< Rate limiting for all forum related actins
   market   ///< Rate limiting for all other actions
};

class account_bandwidth_object : public object< account_bandwidth_object_type, account_bandwidth_object >
{
   public:
      template< typename Constructor, typename Allocator >
      account_bandwidth_object( Constructor&& c, allocator< Allocator > a )
      {
         c( *this );
      }

      account_bandwidth_object() {}

      id_type           id;

      account_name_type account;
      bandwidth_type    type;
      share_type        average_bandwidth;
      share_type        lifetime_bandwidth;
      time_point_sec    last_bandwidth_update;
	  uint16_t			remain_bandwidth_percent = GAMEBANK_100_PERCENT;
};

typedef oid< account_bandwidth_object > account_bandwidth_id_type;

class reserve_ratio_object : public object< reserve_ratio_object_type, reserve_ratio_object >
{
   public:
      template< typename Constructor, typename Allocator >
      reserve_ratio_object( Constructor&& c, allocator< Allocator > a )
      {
         c( *this );
      }

      reserve_ratio_object() {}

      id_type           id;

      /**
       *  Average block size is updated every block to be:
       *
       *     average_block_size = (99 * average_block_size + new_block_size) / 100
       *
       *  This property is used to update the current_reserve_ratio to maintain approximately
       *  50% or less utilization of network capacity.
       */
      int32_t    average_block_size = 0;

      /**
       *   Any time average_block_size <= 50% maximum_block_size this value grows by 1 until it
       *   reaches GAMEBANK_MAX_RESERVE_RATIO.  Any time average_block_size is greater than
       *   50% it falls by 1%.  Upward adjustments happen once per round, downward adjustments
       *   happen every block.
       */
      int64_t    current_reserve_ratio = 1;

      /**
       * The maximum bandwidth the blockchain can support is:
       *
       *    max_bandwidth = maximum_block_size * GAMEBANK_BANDWIDTH_AVERAGE_WINDOW_SECONDS / GAMEBANK_BLOCK_INTERVAL
       *
       * The maximum virtual bandwidth is:
       *
       *    max_bandwidth * current_reserve_ratio
       */
      uint128_t   max_virtual_bandwidth = 0;
};

typedef oid< reserve_ratio_object > reserve_ratio_id_type;

struct by_account_bandwidth_type;

typedef multi_index_container <
   account_bandwidth_object,
   indexed_by <
      ordered_unique< tag< by_id >,
         member< account_bandwidth_object, account_bandwidth_id_type, &account_bandwidth_object::id > >,
      ordered_unique< tag< by_account_bandwidth_type >,
         composite_key< account_bandwidth_object,
            member< account_bandwidth_object, account_name_type, &account_bandwidth_object::account >,
            member< account_bandwidth_object, bandwidth_type, &account_bandwidth_object::type >
         >
      >
   >,
   allocator< account_bandwidth_object >
> account_bandwidth_index;

struct by_account;

typedef multi_index_container <
   reserve_ratio_object,
   indexed_by <
      ordered_unique< tag< by_id >,
         member< reserve_ratio_object, reserve_ratio_id_type, &reserve_ratio_object::id > >
   >,
   allocator< reserve_ratio_object >
> reserve_ratio_index;

} } } // gamebank::plugins::witness

FC_REFLECT_ENUM( gamebank::plugins::witness::bandwidth_type, (none)(forum)(market) )

FC_REFLECT( gamebank::plugins::witness::account_bandwidth_object,
            (id)(account)(type)(average_bandwidth)(lifetime_bandwidth)(last_bandwidth_update)(remain_bandwidth_percent) )
CHAINBASE_SET_INDEX_TYPE( gamebank::plugins::witness::account_bandwidth_object, gamebank::plugins::witness::account_bandwidth_index )

FC_REFLECT( gamebank::plugins::witness::reserve_ratio_object,
            (id)(average_block_size)(current_reserve_ratio)(max_virtual_bandwidth) )
CHAINBASE_SET_INDEX_TYPE( gamebank::plugins::witness::reserve_ratio_object, gamebank::plugins::witness::reserve_ratio_index )
