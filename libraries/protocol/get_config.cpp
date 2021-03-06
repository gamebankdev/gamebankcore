#include <gamebank/protocol/get_config.hpp>
#include <gamebank/protocol/config.hpp>
#include <gamebank/protocol/asset.hpp>
#include <gamebank/protocol/types.hpp>
#include <gamebank/protocol/version.hpp>

namespace gamebank { namespace protocol {

fc::variant_object get_config()
{
   fc::mutable_variant_object result;

#ifdef IS_TEST_NET
   result[ "IS_TEST_NET" ] = true;
   result["TESTNET_BLOCK_LIMIT"] = TESTNET_BLOCK_LIMIT;
#else
   result[ "IS_TEST_NET" ] = false;
#endif

   result["GBD_SYMBOL"] = GBD_SYMBOL;
   result["GAMEBANK_INITIAL_VOTE_POWER_RATE"] = GAMEBANK_INITIAL_VOTE_POWER_RATE;
   result["GAMEBANK_REDUCED_VOTE_POWER_RATE"] = GAMEBANK_REDUCED_VOTE_POWER_RATE;
   result["GAMEBANK_100_PERCENT"] = GAMEBANK_100_PERCENT;
   result["GAMEBANK_1_PERCENT"] = GAMEBANK_1_PERCENT;
   result["GAMEBANK_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD"] = GAMEBANK_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD;
   result["GAMEBANK_ACTIVE_CHALLENGE_COOLDOWN"] = GAMEBANK_ACTIVE_CHALLENGE_COOLDOWN;
   result["GAMEBANK_ACTIVE_CHALLENGE_FEE"] = GAMEBANK_ACTIVE_CHALLENGE_FEE;
   result["GAMEBANK_ADDRESS_PREFIX"] = GAMEBANK_ADDRESS_PREFIX;
   result["GAMEBANK_APR_PERCENT_MULTIPLY_PER_BLOCK"] = GAMEBANK_APR_PERCENT_MULTIPLY_PER_BLOCK;
   result["GAMEBANK_APR_PERCENT_MULTIPLY_PER_HOUR"] = GAMEBANK_APR_PERCENT_MULTIPLY_PER_HOUR;
   result["GAMEBANK_APR_PERCENT_MULTIPLY_PER_ROUND"] = GAMEBANK_APR_PERCENT_MULTIPLY_PER_ROUND;
   result["GAMEBANK_APR_PERCENT_SHIFT_PER_BLOCK"] = GAMEBANK_APR_PERCENT_SHIFT_PER_BLOCK;
   result["GAMEBANK_APR_PERCENT_SHIFT_PER_HOUR"] = GAMEBANK_APR_PERCENT_SHIFT_PER_HOUR;
   result["GAMEBANK_APR_PERCENT_SHIFT_PER_ROUND"] = GAMEBANK_APR_PERCENT_SHIFT_PER_ROUND;
   result["GAMEBANK_BANDWIDTH_AVERAGE_WINDOW_SECONDS"] = GAMEBANK_BANDWIDTH_AVERAGE_WINDOW_SECONDS;
   result["GAMEBANK_BANDWIDTH_PRECISION"] = GAMEBANK_BANDWIDTH_PRECISION;
   result["GAMEBANK_BLOCKCHAIN_PRECISION"] = GAMEBANK_BLOCKCHAIN_PRECISION;
   result["GAMEBANK_BLOCKCHAIN_PRECISION_DIGITS"] = GAMEBANK_BLOCKCHAIN_PRECISION_DIGITS;
   result["GAMEBANK_BLOCKCHAIN_HARDFORK_VERSION"] = GAMEBANK_BLOCKCHAIN_HARDFORK_VERSION;
   result["GAMEBANK_BLOCKCHAIN_VERSION"] = GAMEBANK_BLOCKCHAIN_VERSION;
   result["GAMEBANK_BLOCK_INTERVAL"] = GAMEBANK_BLOCK_INTERVAL;
   result["GAMEBANK_BLOCKS_PER_DAY"] = GAMEBANK_BLOCKS_PER_DAY;
   result["GAMEBANK_BLOCKS_PER_HOUR"] = GAMEBANK_BLOCKS_PER_HOUR;
   result["GAMEBANK_BLOCKS_PER_YEAR"] = GAMEBANK_BLOCKS_PER_YEAR;
   result["GAMEBANK_CASHOUT_WINDOW_SECONDS"] = GAMEBANK_CASHOUT_WINDOW_SECONDS;
   result["GAMEBANK_CHAIN_ID"] = GAMEBANK_CHAIN_ID;
   result["GAMEBANK_CHAIN_ID_NAME"] = GAMEBANK_CHAIN_ID_NAME;
   result["GAMEBANK_COMMENT_REWARD_FUND_NAME"] = GAMEBANK_COMMENT_REWARD_FUND_NAME;
   result["GAMEBANK_CONTENT_APR_PERCENT"] = GAMEBANK_CONTENT_APR_PERCENT;
   result["GAMEBANK_CONTENT_CONSTANT_HF0"] = GAMEBANK_CONTENT_CONSTANT_HF0;
   result["GAMEBANK_CONTENT_REWARD_PERCENT"] = GAMEBANK_CONTENT_REWARD_PERCENT;
   result["GAMEBANK_CONVERSION_DELAY"] = GAMEBANK_CONVERSION_DELAY;
   result["GAMEBANK_CREATE_ACCOUNT_DELEGATION_RATIO"] = GAMEBANK_CREATE_ACCOUNT_DELEGATION_RATIO;
   result["GAMEBANK_CREATE_ACCOUNT_DELEGATION_TIME"] = GAMEBANK_CREATE_ACCOUNT_DELEGATION_TIME;
   result["GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER"] = GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER;
   result["GAMEBANK_CURATE_APR_PERCENT"] = GAMEBANK_CURATE_APR_PERCENT;
   result["GAMEBANK_DEFAULT_SBD_INTEREST_RATE"] = GAMEBANK_DEFAULT_GBD_INTEREST_RATE;
   result["GAMEBANK_EQUIHASH_K"] = GAMEBANK_EQUIHASH_K;
   result["GAMEBANK_EQUIHASH_N"] = GAMEBANK_EQUIHASH_N;
   result["GAMEBANK_FEED_HISTORY_WINDOW"] = GAMEBANK_FEED_HISTORY_WINDOW;
   result["GAMEBANK_FEED_INTERVAL_BLOCKS"] = GAMEBANK_FEED_INTERVAL_BLOCKS;
   result["GAMEBANK_GENESIS_TIME"] = GAMEBANK_GENESIS_TIME;
   result["GAMEBANK_HARDFORK_REQUIRED_WITNESSES"] = GAMEBANK_HARDFORK_REQUIRED_WITNESSES;
   result["GAMEBANK_INFLATION_NARROWING_PERIOD"] = GAMEBANK_INFLATION_NARROWING_PERIOD;
   result["GAMEBANK_INFLATION_RATE_START_PERCENT"] = GAMEBANK_INFLATION_RATE_START_PERCENT;
   result["GAMEBANK_INFLATION_RATE_STOP_PERCENT"] = GAMEBANK_INFLATION_RATE_STOP_PERCENT;
   result["GAMEBANK_INIT_MINER_NAME"] = GAMEBANK_INIT_MINER_NAME;
   result["GAMEBANK_INIT_PUBLIC_KEY_STR"] = GAMEBANK_INIT_PUBLIC_KEY_STR;
#if 0
   // do not expose private key, period.
   // we need this line present but inactivated so CI check for all constants in config.hpp doesn't complain.
   result["GAMEBANK_INIT_PRIVATE_KEY"] = GAMEBANK_INIT_PRIVATE_KEY;
#endif
   result["GAMEBANK_INIT_SUPPLY"] = GAMEBANK_INIT_SUPPLY;
   result["GAMEBANK_INIT_TIME"] = GAMEBANK_INIT_TIME;
   result["GAMEBANK_IRREVERSIBLE_THRESHOLD"] = GAMEBANK_IRREVERSIBLE_THRESHOLD;
   result["GAMEBANK_LIQUIDITY_APR_PERCENT"] = GAMEBANK_LIQUIDITY_APR_PERCENT;
   result["GAMEBANK_LIQUIDITY_REWARD_BLOCKS"] = GAMEBANK_LIQUIDITY_REWARD_BLOCKS;
   result["GAMEBANK_LIQUIDITY_REWARD_PERIOD_SEC"] = GAMEBANK_LIQUIDITY_REWARD_PERIOD_SEC;
   result["GAMEBANK_LIQUIDITY_TIMEOUT_SEC"] = GAMEBANK_LIQUIDITY_TIMEOUT_SEC;
   result["GAMEBANK_MAX_ACCOUNT_NAME_LENGTH"] = GAMEBANK_MAX_ACCOUNT_NAME_LENGTH;
   result["GAMEBANK_MAX_ACCOUNT_WITNESS_VOTES"] = GAMEBANK_MAX_ACCOUNT_WITNESS_VOTES;
   result["GAMEBANK_MAX_ASSET_WHITELIST_AUTHORITIES"] = GAMEBANK_MAX_ASSET_WHITELIST_AUTHORITIES;
   result["GAMEBANK_MAX_AUTHORITY_MEMBERSHIP"] = GAMEBANK_MAX_AUTHORITY_MEMBERSHIP;
   result["GAMEBANK_MAX_BLOCK_SIZE"] = GAMEBANK_MAX_BLOCK_SIZE;
   result["GAMEBANK_SOFT_MAX_BLOCK_SIZE"] = GAMEBANK_SOFT_MAX_BLOCK_SIZE;
   result["GAMEBANK_MAX_CASHOUT_WINDOW_SECONDS"] = GAMEBANK_MAX_CASHOUT_WINDOW_SECONDS;
   result["GAMEBANK_MAX_COMMENT_DEPTH"] = GAMEBANK_MAX_COMMENT_DEPTH;
   result["GAMEBANK_MAX_FEED_AGE_SECONDS"] = GAMEBANK_MAX_FEED_AGE_SECONDS;
   result["GAMEBANK_MAX_INSTANCE_ID"] = GAMEBANK_MAX_INSTANCE_ID;
   result["GAMEBANK_MAX_MEMO_SIZE"] = GAMEBANK_MAX_MEMO_SIZE;
   result["GAMEBANK_MAX_WITNESSES"] = GAMEBANK_MAX_WITNESSES;
   result["GAMEBANK_MAX_MINER_WITNESSES"] = GAMEBANK_MAX_MINER_WITNESSES;
   result["GAMEBANK_MAX_PERMLINK_LENGTH"] = GAMEBANK_MAX_PERMLINK_LENGTH;
   result["GAMEBANK_MAX_PROXY_RECURSION_DEPTH"] = GAMEBANK_MAX_PROXY_RECURSION_DEPTH;
   result["GAMEBANK_MAX_RATION_DECAY_RATE"] = GAMEBANK_MAX_RATION_DECAY_RATE;
   result["GAMEBANK_MAX_RESERVE_RATIO"] = GAMEBANK_MAX_RESERVE_RATIO;
   result["GAMEBANK_MAX_RUNNER_WITNESSES"] = GAMEBANK_MAX_RUNNER_WITNESSES;
   result["GAMEBANK_MAX_SATOSHIS"] = GAMEBANK_MAX_SATOSHIS;
   result["GAMEBANK_MAX_SHARE_SUPPLY"] = GAMEBANK_MAX_SHARE_SUPPLY;
   result["GAMEBANK_MAX_SIG_CHECK_DEPTH"] = GAMEBANK_MAX_SIG_CHECK_DEPTH;
   result["GAMEBANK_MAX_TIME_UNTIL_EXPIRATION"] = GAMEBANK_MAX_TIME_UNTIL_EXPIRATION;
   result["GAMEBANK_MAX_TRANSACTION_SIZE"] = GAMEBANK_MAX_TRANSACTION_SIZE;
   result["GAMEBANK_MAX_UNDO_HISTORY"] = GAMEBANK_MAX_UNDO_HISTORY;
   result["GAMEBANK_MAX_URL_LENGTH"] = GAMEBANK_MAX_URL_LENGTH;
   result["GAMEBANK_MAX_VOTE_CHANGES"] = GAMEBANK_MAX_VOTE_CHANGES;
   result["GAMEBANK_MAX_VOTED_WITNESSES"] = GAMEBANK_MAX_VOTED_WITNESSES;
   result["GAMEBANK_MAX_WITHDRAW_ROUTES"] = GAMEBANK_MAX_WITHDRAW_ROUTES;
   result["GAMEBANK_MAX_WITNESS_URL_LENGTH"] = GAMEBANK_MAX_WITNESS_URL_LENGTH;
   result["GAMEBANK_MIN_ACCOUNT_CREATION_FEE"] = GAMEBANK_MIN_ACCOUNT_CREATION_FEE;
   result["GAMEBANK_MIN_ACCOUNT_NAME_LENGTH"] = GAMEBANK_MIN_ACCOUNT_NAME_LENGTH;
   result["GAMEBANK_MIN_BLOCK_SIZE_LIMIT"] = GAMEBANK_MIN_BLOCK_SIZE_LIMIT;
   result["GAMEBANK_MIN_BLOCK_SIZE"] = GAMEBANK_MIN_BLOCK_SIZE;
   result["GAMEBANK_MIN_CONTENT_REWARD"] = GAMEBANK_MIN_CONTENT_REWARD;
   result["GAMEBANK_MIN_CURATE_REWARD"] = GAMEBANK_MIN_CURATE_REWARD;
   result["GAMEBANK_MIN_PERMLINK_LENGTH"] = GAMEBANK_MIN_PERMLINK_LENGTH;
   result["GAMEBANK_MIN_REPLY_INTERVAL"] = GAMEBANK_MIN_REPLY_INTERVAL;
   result["GAMEBANK_MIN_REPLY_INTERVAL_HF01"] = GAMEBANK_MIN_REPLY_INTERVAL_HF01;
   result["GAMEBANK_MIN_ROOT_COMMENT_INTERVAL"] = GAMEBANK_MIN_ROOT_COMMENT_INTERVAL;
   result["GAMEBANK_MIN_VOTE_INTERVAL_SEC"] = GAMEBANK_MIN_VOTE_INTERVAL_SEC;
   result["GAMEBANK_MINER_ACCOUNT"] = GAMEBANK_MINER_ACCOUNT;
   result["GAMEBANK_MINER_PAY_PERCENT"] = GAMEBANK_MINER_PAY_PERCENT;
   result["GAMEBANK_MIN_FEEDS"] = GAMEBANK_MIN_FEEDS;
   result["GAMEBANK_MINING_REWARD"] = GAMEBANK_MINING_REWARD;
   result["GAMEBANK_MINING_TIME"] = GAMEBANK_MINING_TIME;
   result["GAMEBANK_MIN_LIQUIDITY_REWARD"] = GAMEBANK_MIN_LIQUIDITY_REWARD;
   result["GAMEBANK_MIN_LIQUIDITY_REWARD_PERIOD_SEC"] = GAMEBANK_MIN_LIQUIDITY_REWARD_PERIOD_SEC;
   result["GAMEBANK_MIN_PAYOUT_SBD"] = GAMEBANK_MIN_PAYOUT_GBD;
   result["GAMEBANK_MIN_POW_REWARD"] = GAMEBANK_MIN_POW_REWARD;
   result["GAMEBANK_MIN_PRODUCER_REWARD"] = GAMEBANK_MIN_PRODUCER_REWARD;
   result["GAMEBANK_MIN_TRANSACTION_EXPIRATION_LIMIT"] = GAMEBANK_MIN_TRANSACTION_EXPIRATION_LIMIT;
   result["GAMEBANK_MIN_TRANSACTION_SIZE_LIMIT"] = GAMEBANK_MIN_TRANSACTION_SIZE_LIMIT;
   result["GAMEBANK_MIN_UNDO_HISTORY"] = GAMEBANK_MIN_UNDO_HISTORY;
   result["GAMEBANK_NULL_ACCOUNT"] = GAMEBANK_NULL_ACCOUNT;
   result["GAMEBANK_NUM_INIT_MINERS"] = GAMEBANK_NUM_INIT_MINERS;
   result["GAMEBANK_OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM"] = GAMEBANK_OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM;
   result["GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD"] = GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD;
   result["GAMEBANK_OWNER_CHALLENGE_COOLDOWN"] = GAMEBANK_OWNER_CHALLENGE_COOLDOWN;
   result["GAMEBANK_OWNER_CHALLENGE_FEE"] = GAMEBANK_OWNER_CHALLENGE_FEE;
   result["GAMEBANK_OWNER_UPDATE_LIMIT"] = GAMEBANK_OWNER_UPDATE_LIMIT;
   result["GAMEBANK_POST_AVERAGE_WINDOW"] = GAMEBANK_POST_AVERAGE_WINDOW;
   result["GAMEBANK_POST_REWARD_FUND_NAME"] = GAMEBANK_POST_REWARD_FUND_NAME;
   result["GAMEBANK_POST_WEIGHT_CONSTANT"] = GAMEBANK_POST_WEIGHT_CONSTANT;
   result["GAMEBANK_POW_APR_PERCENT"] = GAMEBANK_POW_APR_PERCENT;
   result["GAMEBANK_PRODUCER_APR_PERCENT"] = GAMEBANK_PRODUCER_APR_PERCENT;
   result["GAMEBANK_PROXY_TO_SELF_ACCOUNT"] = GAMEBANK_PROXY_TO_SELF_ACCOUNT;
   result["GAMEBANK_SBD_INTEREST_COMPOUND_INTERVAL_SEC"] = GAMEBANK_GBD_INTEREST_COMPOUND_INTERVAL_SEC;
   result["GAMEBANK_SECONDS_PER_YEAR"] = GAMEBANK_SECONDS_PER_YEAR;
   result["GAMEBANK_RECENT_RSHARES_DECAY_TIME"] = GAMEBANK_RECENT_RSHARES_DECAY_TIME;
   result["GAMEBANK_REVERSE_AUCTION_WINDOW_SECONDS"] = GAMEBANK_REVERSE_AUCTION_WINDOW_SECONDS;
   result["GAMEBANK_ROOT_POST_PARENT"] = GAMEBANK_ROOT_POST_PARENT;
   result["GAMEBANK_SAVINGS_WITHDRAW_REQUEST_LIMIT"] = GAMEBANK_SAVINGS_WITHDRAW_REQUEST_LIMIT;
   result["GAMEBANK_SAVINGS_WITHDRAW_TIME"] = GAMEBANK_SAVINGS_WITHDRAW_TIME;
   result["GAMEBANK_SBD_START_PERCENT"] = GAMEBANK_GBD_START_PERCENT;
   result["GAMEBANK_SBD_STOP_PERCENT"] = GAMEBANK_GBD_STOP_PERCENT;
   result["GAMEBANK_SECOND_CASHOUT_WINDOW"] = GAMEBANK_SECOND_CASHOUT_WINDOW;
   result["GAMEBANK_SOFT_MAX_COMMENT_DEPTH"] = GAMEBANK_SOFT_MAX_COMMENT_DEPTH;
   result["GAMEBANK_START_MINER_VOTING_BLOCK"] = GAMEBANK_START_MINER_VOTING_BLOCK;
   result["GAMEBANK_START_VESTING_BLOCK"] = GAMEBANK_START_VESTING_BLOCK;
   result["GAMEBANK_TEMP_ACCOUNT"] = GAMEBANK_TEMP_ACCOUNT;
   result["GAMEBANK_UPVOTE_LOCKOUT_TIME"] = GAMEBANK_UPVOTE_LOCKOUT_TIME;
   result["GAMEBANK_VESTING_FUND_PERCENT"] = GAMEBANK_VESTING_FUND_PERCENT;
   result["GAMEBANK_VESTING_WITHDRAW_INTERVALS"] = GAMEBANK_VESTING_WITHDRAW_INTERVALS;
   result["GAMEBANK_VESTING_WITHDRAW_INTERVAL_SECONDS"] = GAMEBANK_VESTING_WITHDRAW_INTERVAL_SECONDS;
   result["GAMEBANK_VOTE_DUST_THRESHOLD"] = GAMEBANK_VOTE_DUST_THRESHOLD;
   result["GAMEBANK_VOTE_REGENERATION_SECONDS"] = GAMEBANK_VOTE_REGENERATION_SECONDS;
   result["GBC_SYMBOL"] = GBC_SYMBOL;
   result["GBS_SYMBOL"] = GBS_SYMBOL;
   result["GAMEBANK_VIRTUAL_SCHEDULE_LAP_LENGTH"] = GAMEBANK_VIRTUAL_SCHEDULE_LAP_LENGTH;
   result["GAMEBANK_VIRTUAL_SCHEDULE_LAP_LENGTH2"] = GAMEBANK_VIRTUAL_SCHEDULE_LAP_LENGTH2;
   result["GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION"] = GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION;
   result["GAMEBANK_DELEGATION_RETURN_PERIOD"] = GAMEBANK_DELEGATION_RETURN_PERIOD;
   result["GAMEBANK_DELEGATION_RETURN_PERIOD_HF20"] = GAMEBANK_DELEGATION_RETURN_PERIOD_HF01;

   return result;
}

} } // gamebank::protocol
