#pragma once

#include <gamebank/protocol/gamebank_operations.hpp>

#include <gamebank/chain/evaluator.hpp>

namespace gamebank { namespace chain {

using namespace gamebank::protocol;

GAMEBANK_DEFINE_EVALUATOR( account_create )
GAMEBANK_DEFINE_EVALUATOR( account_create_with_delegation )
GAMEBANK_DEFINE_EVALUATOR( account_update )
GAMEBANK_DEFINE_EVALUATOR( transfer )
GAMEBANK_DEFINE_EVALUATOR( transfer_to_vesting )
GAMEBANK_DEFINE_EVALUATOR( witness_update )
GAMEBANK_DEFINE_EVALUATOR( account_witness_vote )
GAMEBANK_DEFINE_EVALUATOR( account_witness_proxy )
GAMEBANK_DEFINE_EVALUATOR( withdraw_vesting )
GAMEBANK_DEFINE_EVALUATOR( set_withdraw_vesting_route )
GAMEBANK_DEFINE_EVALUATOR( comment )
GAMEBANK_DEFINE_EVALUATOR( comment_options )
GAMEBANK_DEFINE_EVALUATOR( delete_comment )
GAMEBANK_DEFINE_EVALUATOR( vote )
GAMEBANK_DEFINE_EVALUATOR( custom )
GAMEBANK_DEFINE_EVALUATOR( custom_json )
GAMEBANK_DEFINE_EVALUATOR( custom_binary )
GAMEBANK_DEFINE_EVALUATOR( pow )
GAMEBANK_DEFINE_EVALUATOR( pow2 )
GAMEBANK_DEFINE_EVALUATOR( feed_publish )
GAMEBANK_DEFINE_EVALUATOR( convert )
GAMEBANK_DEFINE_EVALUATOR( limit_order_create )
GAMEBANK_DEFINE_EVALUATOR( limit_order_cancel )
GAMEBANK_DEFINE_EVALUATOR( report_over_production )
GAMEBANK_DEFINE_EVALUATOR( limit_order_create2 )
GAMEBANK_DEFINE_EVALUATOR( escrow_transfer )
GAMEBANK_DEFINE_EVALUATOR( escrow_approve )
GAMEBANK_DEFINE_EVALUATOR( escrow_dispute )
GAMEBANK_DEFINE_EVALUATOR( escrow_release )
GAMEBANK_DEFINE_EVALUATOR( claim_account )
GAMEBANK_DEFINE_EVALUATOR( create_claimed_account )
GAMEBANK_DEFINE_EVALUATOR( request_account_recovery )
GAMEBANK_DEFINE_EVALUATOR( recover_account )
GAMEBANK_DEFINE_EVALUATOR( change_recovery_account )
GAMEBANK_DEFINE_EVALUATOR( transfer_to_savings )
GAMEBANK_DEFINE_EVALUATOR( transfer_from_savings )
GAMEBANK_DEFINE_EVALUATOR( cancel_transfer_from_savings )
GAMEBANK_DEFINE_EVALUATOR( decline_voting_rights )
GAMEBANK_DEFINE_EVALUATOR( reset_account )
GAMEBANK_DEFINE_EVALUATOR( set_reset_account )
GAMEBANK_DEFINE_EVALUATOR( claim_reward_balance )
GAMEBANK_DEFINE_EVALUATOR( delegate_vesting_shares )
GAMEBANK_DEFINE_EVALUATOR( witness_set_properties )
GAMEBANK_DEFINE_EVALUATOR( crowdfunding )
GAMEBANK_DEFINE_EVALUATOR( invest )
GAMEBANK_DEFINE_EVALUATOR( nonfungible_fund_create )
GAMEBANK_DEFINE_EVALUATOR( nonfungible_fund_transfer )
GAMEBANK_DEFINE_EVALUATOR( nonfungible_fund_put_up_for_sale )
GAMEBANK_DEFINE_EVALUATOR( nonfungible_fund_withdraw_from_sale )
GAMEBANK_DEFINE_EVALUATOR( nonfungible_fund_buy )

GAMEBANK_DEFINE_EVALUATOR( contract_deploy )
GAMEBANK_DEFINE_EVALUATOR( contract_call )

} } // gamebank::chain
