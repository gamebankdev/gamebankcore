#pragma once

#include <gamebank/protocol/types.hpp>

#include <gamebank/protocol/operation_util.hpp>
#include <gamebank/protocol/gamebank_operations.hpp>
#include <gamebank/protocol/gamebank_virtual_operations.hpp>

namespace gamebank { namespace protocol {

   /** NOTE: do not change the order of any operations prior to the virtual operations
    * or it will trigger a hardfork.
    */
   typedef fc::static_variant<
            vote_operation,
            comment_operation,

            transfer_operation,
            transfer_to_vesting_operation,
            withdraw_vesting_operation,

            limit_order_create_operation,
            limit_order_cancel_operation,

            feed_publish_operation,
            convert_operation,

            account_create_operation,
            account_update_operation,

            witness_update_operation,
            account_witness_vote_operation,
            account_witness_proxy_operation,

            pow_operation,

            custom_operation,

            report_over_production_operation,

            delete_comment_operation,
            custom_json_operation,
            comment_options_operation,
            set_withdraw_vesting_route_operation,
            limit_order_create2_operation,
            claim_account_operation,
            create_claimed_account_operation,
            request_account_recovery_operation,
            recover_account_operation,
            change_recovery_account_operation,
            escrow_transfer_operation,
            escrow_dispute_operation,
            escrow_release_operation,
            pow2_operation,
            escrow_approve_operation,
            transfer_to_savings_operation,
            transfer_from_savings_operation,
            cancel_transfer_from_savings_operation,
            custom_binary_operation,
            decline_voting_rights_operation,
            reset_account_operation,
            set_reset_account_operation,
            claim_reward_balance_operation,
            delegate_vesting_shares_operation,
            account_create_with_delegation_operation,
            witness_set_properties_operation,
            crowdfunding_operation,
            invest_operation,
			nonfungible_fund_create_operation,
			nonfungible_fund_transfer_operation,
			nonfungible_fund_put_up_for_sale_operation,
			nonfungible_fund_withdraw_from_sale_operation,
			nonfungible_fund_buy_operation,
	        contract_deploy_operation,
	        contract_call_operation,

            /// virtual operations below this point
            fill_convert_request_operation,
            author_reward_operation,
            curation_reward_operation,
            comment_reward_operation,
            liquidity_reward_operation,
            interest_operation,
            fill_vesting_withdraw_operation,
            fill_order_operation,
            shutdown_witness_operation,
            fill_transfer_from_savings_operation,
            hardfork_operation,
            comment_payout_update_operation,
            return_vesting_delegation_operation,
            comment_benefactor_reward_operation,
            producer_reward_operation
         > operation;

   /*void operation_get_required_authorities( const operation& op,
                                            flat_set<string>& active,
                                            flat_set<string>& owner,
                                            flat_set<string>& posting,
                                            vector<authority>&  other );

   void operation_validate( const operation& op );*/

   bool is_market_operation( const operation& op );

   bool is_virtual_operation( const operation& op );

} } // gamebank::protocol

/*namespace fc {
    void to_variant( const gamebank::protocol::operation& var,  fc::variant& vo );
    void from_variant( const fc::variant& var,  gamebank::protocol::operation& vo );
}*/

GAMEBANK_DECLARE_OPERATION_TYPE( gamebank::protocol::operation )
FC_REFLECT_TYPENAME( gamebank::protocol::operation )
