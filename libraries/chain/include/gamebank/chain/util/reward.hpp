#pragma once

#include <gamebank/chain/util/asset.hpp>
#include <gamebank/chain/gamebank_objects.hpp>

#include <gamebank/protocol/asset.hpp>
#include <gamebank/protocol/config.hpp>
#include <gamebank/protocol/types.hpp>
#include <gamebank/protocol/misc_utilities.hpp>

#include <fc/reflect/reflect.hpp>

#include <fc/uint128.hpp>

namespace gamebank { namespace chain { namespace util {

using gamebank::protocol::asset;
using gamebank::protocol::price;
using gamebank::protocol::share_type;

using fc::uint128_t;

struct comment_reward_context
{
   share_type rshares;
   uint16_t   reward_weight = 0;
   asset      max_gbd;
   uint128_t  total_reward_shares2;
   asset      total_reward_fund_gbc;
   price      current_gbc_price;
   protocol::curve_id   reward_curve = protocol::quadratic;
   uint128_t  content_constant = GAMEBANK_CONTENT_CONSTANT_HF0;
};

uint64_t get_rshare_reward( const comment_reward_context& ctx );

inline uint128_t get_content_constant_s()
{
   return GAMEBANK_CONTENT_CONSTANT_HF0; // looking good for posters
}

uint128_t evaluate_reward_curve( const uint128_t& rshares, const protocol::curve_id& curve = protocol::quadratic, const uint128_t& content_constant = GAMEBANK_CONTENT_CONSTANT_HF0 );

inline bool is_comment_payout_dust( const price& p, uint64_t gbc_payout )
{
   return to_gbd( p, asset( gbc_payout, GBC_SYMBOL ) ) < GAMEBANK_MIN_PAYOUT_GBD;
}

} } } // gamebank::chain::util

FC_REFLECT( gamebank::chain::util::comment_reward_context,
   (rshares)
   (reward_weight)
   (max_gbd)
   (total_reward_shares2)
   (total_reward_fund_gbc)
   (current_gbc_price)
   (reward_curve)
   (content_constant)
   )
