#pragma once
#include <gamebank/protocol/hardfork.hpp>

// WARNING!
// Every symbol defined here needs to be handled appropriately in get_config.cpp
// This is checked by get_config_check.sh called from Dockerfile

#ifdef IS_TEST_NET
#define GAMEBANK_BLOCKCHAIN_VERSION              ( version(0, 1, 0) )
#define GAMEBANK_INIT_PUBLIC_KEY_STR             "TST6MH4Q1JbHp2AYmBpuux6nAbiQEW4nyunbVAdHpgWPoYPushxCo"
#define GAMEBANK_CHAIN_ID_NAME "testnet"
#define GAMEBANK_CHAIN_ID (fc::sha256::hash(GAMEBANK_CHAIN_ID_NAME))
#define GAMEBANK_ADDRESS_PREFIX                  "TST"

#define GAMEBANK_GENESIS_TIME                    (fc::time_point_sec(1451606400))
#define GAMEBANK_MINING_TIME                     (fc::time_point_sec(1451606400))
#define GAMEBANK_CASHOUT_WINDOW_SECONDS          (60*60*24*7) /// 7 days
#define GAMEBANK_SECOND_CASHOUT_WINDOW           (60*60*24*3) /// 3 days
#define GAMEBANK_MAX_CASHOUT_WINDOW_SECONDS      (60*60*24) /// 1 day
#define GAMEBANK_UPVOTE_LOCKOUT_TIME             (fc::minutes(5))


#define GAMEBANK_MIN_ACCOUNT_CREATION_FEE          (int64_t( 10000 ) * int64_t( 1000 ))			//percision 10^3

#define GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD                  fc::seconds(60)
#define GAMEBANK_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD  fc::seconds(12)
#define GAMEBANK_OWNER_UPDATE_LIMIT                          fc::seconds(0)
#define GAMEBANK_OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM 1

#define GAMEBANK_INIT_SUPPLY                     (int64_t( 5000 ) * int64_t( 1000000 ) * int64_t( 1000 ))//5 billion, percision 10^3
#define GAMEBANK_INIT_VESTING_SUPPLY_PERCENT	 (10 * GAMEBANK_1_PERCENT) 

/// Allows to limit number of total produced blocks.
#define TESTNET_BLOCK_LIMIT                   (3000000)

#else // IS LIVE GAMEBANK NETWORK

#define GAMEBANK_BLOCKCHAIN_VERSION              ( version(0, 0, 1) )

#define GAMEBANK_INIT_PUBLIC_KEY_STR             "TST6MH4Q1JbHp2AYmBpuux6nAbiQEW4nyunbVAdHpgWPoYPushxCo"
#define GAMEBANK_CHAIN_ID_NAME ""
#define GAMEBANK_CHAIN_ID fc::sha256()
#define GAMEBANK_ADDRESS_PREFIX                  "TST"

#define GAMEBANK_GENESIS_TIME                    (fc::time_point_sec(1458835200))
#define GAMEBANK_MINING_TIME                     (fc::time_point_sec(1458838800))
#define GAMEBANK_CASHOUT_WINDOW_SECONDS          (60*60*24*7)  /// 7 days
#define GAMEBANK_SECOND_CASHOUT_WINDOW           (60*60*24*30) /// 30 days
#define GAMEBANK_MAX_CASHOUT_WINDOW_SECONDS      (60*60*24*14) /// 2 weeks
#define GAMEBANK_UPVOTE_LOCKOUT_TIME             (fc::hours(12))

#define GAMEBANK_MIN_ACCOUNT_CREATION_FEE           1

#define GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD                  fc::days(30)
#define GAMEBANK_ACCOUNT_RECOVERY_REQUEST_EXPIRATION_PERIOD  fc::days(1)
#define GAMEBANK_OWNER_UPDATE_LIMIT                          fc::minutes(60)
#define GAMEBANK_OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM 3186477

#define GAMEBANK_INIT_SUPPLY                     int64_t(0)

#endif

#define GBS_SYMBOL  (gamebank::protocol::asset_symbol_type::from_asset_num( GAMEBANK_ASSET_NUM_GBS ) )
#define GBC_SYMBOL  (gamebank::protocol::asset_symbol_type::from_asset_num( GAMEBANK_ASSET_NUM_GBC ) )
#define GBD_SYMBOL    (gamebank::protocol::asset_symbol_type::from_asset_num( GAMEBANK_ASSET_NUM_GBD ) )

#define GAMEBANK_BLOCKCHAIN_HARDFORK_VERSION     ( hardfork_version( GAMEBANK_BLOCKCHAIN_VERSION ) )

#define GAMEBANK_BLOCK_INTERVAL                  3
#define GAMEBANK_BLOCKS_PER_YEAR                 (365*24*60*60/GAMEBANK_BLOCK_INTERVAL)
#define GAMEBANK_BLOCKS_PER_DAY                  (24*60*60/GAMEBANK_BLOCK_INTERVAL)
#define GAMEBANK_START_VESTING_BLOCK             (GAMEBANK_BLOCKS_PER_DAY * 7)
#define GAMEBANK_START_MINER_VOTING_BLOCK        (GAMEBANK_BLOCKS_PER_DAY * 30)

#define GAMEBANK_INIT_MINER_NAME                 "initminer"
#define GAMEBANK_NUM_INIT_MINERS                 1
#define GAMEBANK_INIT_TIME                       (fc::time_point_sec());

#define GAMEBANK_MAX_WITNESSES                   21

#define GAMEBANK_MAX_VOTED_WITNESSES                20
#define GAMEBANK_MAX_MINER_WITNESSES                0
#define GAMEBANK_MAX_RUNNER_WITNESSES               1

#define GAMEBANK_HARDFORK_REQUIRED_WITNESSES     17 // 17 of the 21 dpos witnesses (20 elected and 1 virtual time) required for hardfork. This guarantees 75% participation on all subsequent rounds.
#define GAMEBANK_MAX_TIME_UNTIL_EXPIRATION       (60*60) // seconds,  aka: 1 hour
#define GAMEBANK_MAX_MEMO_SIZE                   2048
#define GAMEBANK_MAX_PROXY_RECURSION_DEPTH       4
#define GAMEBANK_VESTING_WITHDRAW_INTERVALS      13
#define GAMEBANK_VESTING_WITHDRAW_INTERVAL_SECONDS (60*60*24*7) /// 1 week per interval
#define GAMEBANK_MAX_WITHDRAW_ROUTES             10
#define GAMEBANK_SAVINGS_WITHDRAW_TIME        	(fc::days(3))
#define GAMEBANK_SAVINGS_WITHDRAW_REQUEST_LIMIT  100
#define GAMEBANK_VOTE_REGENERATION_SECONDS       (5*60*60*24) // 5 day
#define GAMEBANK_MAX_VOTE_CHANGES                5
#define GAMEBANK_REVERSE_AUCTION_WINDOW_SECONDS  (60*30) /// 30 minutes
#define GAMEBANK_MIN_VOTE_INTERVAL_SEC           3
#define GAMEBANK_VOTE_DUST_THRESHOLD             (50000000)

#define GAMEBANK_MIN_ROOT_COMMENT_INTERVAL       (fc::seconds(60*5)) // 5 minutes
#define GAMEBANK_MIN_REPLY_INTERVAL              (fc::seconds(20)) // 20 seconds
#define GAMEBANK_MIN_REPLY_INTERVAL_HF01         (fc::seconds(3)) // 3 seconds
#define GAMEBANK_POST_AVERAGE_WINDOW             (60*60*24u) // 1 day
#define GAMEBANK_POST_WEIGHT_CONSTANT            (uint64_t(4*GAMEBANK_100_PERCENT) * (4*GAMEBANK_100_PERCENT))// (4*GAMEBANK_100_PERCENT) -> 2 posts per 1 days, average 1 every 12 hours

#define GAMEBANK_MAX_ACCOUNT_WITNESS_VOTES       30

#define GAMEBANK_100_PERCENT                     10000
#define GAMEBANK_1_PERCENT                       (GAMEBANK_100_PERCENT/100)
#define GAMEBANK_DEFAULT_GBD_INTEREST_RATE       (10*GAMEBANK_1_PERCENT) ///< 10% APR

#define GAMEBANK_INFLATION_RATE_START_PERCENT    (978) // Fixes block 7,000,000 to 9.5%
#define GAMEBANK_INFLATION_RATE_STOP_PERCENT     (95) // 0.95%
#define GAMEBANK_INFLATION_NARROWING_PERIOD      (250000) // Narrow 0.01% every 250k blocks
#define GAMEBANK_CONTENT_REWARD_PERCENT          (75*GAMEBANK_1_PERCENT) //75% of inflation, 7.125% inflation
#define GAMEBANK_VESTING_FUND_PERCENT            (15*GAMEBANK_1_PERCENT) //15% of inflation, 1.425% inflation

#define GAMEBANK_MINER_PAY_PERCENT               (GAMEBANK_1_PERCENT) // 1%
#define GAMEBANK_MAX_RATION_DECAY_RATE           (1000000)

#define GAMEBANK_BANDWIDTH_AVERAGE_WINDOW_SECONDS (60*60*24*7) ///< 1 week
#define GAMEBANK_BANDWIDTH_PRECISION             (uint64_t(1000000)) ///< 1 million
#define GAMEBANK_MAX_COMMENT_DEPTH               0xffff // 64k
#define GAMEBANK_SOFT_MAX_COMMENT_DEPTH          0xff // 255

#define GAMEBANK_MAX_RESERVE_RATIO               (20000)

#define GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER 30
#define GAMEBANK_CREATE_ACCOUNT_DELEGATION_RATIO    5
#define GAMEBANK_CREATE_ACCOUNT_DELEGATION_TIME     fc::days(30)

#define GAMEBANK_MINING_REWARD                   asset( 1000, GBC_SYMBOL )
#define GAMEBANK_EQUIHASH_N                      140
#define GAMEBANK_EQUIHASH_K                      6

#define GAMEBANK_LIQUIDITY_TIMEOUT_SEC           (fc::seconds(60*60*24*7)) // After one week volume is set to 0
#define GAMEBANK_MIN_LIQUIDITY_REWARD_PERIOD_SEC (fc::seconds(60)) // 1 minute required on books to receive volume
#define GAMEBANK_LIQUIDITY_REWARD_PERIOD_SEC     (60*60)
#define GAMEBANK_LIQUIDITY_REWARD_BLOCKS         (GAMEBANK_LIQUIDITY_REWARD_PERIOD_SEC/GAMEBANK_BLOCK_INTERVAL)
#define GAMEBANK_MIN_LIQUIDITY_REWARD            (asset( 1000*GAMEBANK_LIQUIDITY_REWARD_BLOCKS, GBC_SYMBOL )) // Minumum reward to be paid out to liquidity providers
#define GAMEBANK_MIN_CONTENT_REWARD              GAMEBANK_MINING_REWARD
#define GAMEBANK_MIN_CURATE_REWARD               GAMEBANK_MINING_REWARD
#define GAMEBANK_MIN_PRODUCER_REWARD             GAMEBANK_MINING_REWARD
#define GAMEBANK_MIN_POW_REWARD                  GAMEBANK_MINING_REWARD

#define GAMEBANK_ACTIVE_CHALLENGE_FEE            asset( 2000, GBC_SYMBOL )
#define GAMEBANK_OWNER_CHALLENGE_FEE             asset( 30000, GBC_SYMBOL )
#define GAMEBANK_ACTIVE_CHALLENGE_COOLDOWN       fc::days(1)
#define GAMEBANK_OWNER_CHALLENGE_COOLDOWN        fc::days(1)

#define GAMEBANK_POST_REWARD_FUND_NAME           ("post")
#define GAMEBANK_COMMENT_REWARD_FUND_NAME        ("comment")
#define GAMEBANK_RECENT_RSHARES_DECAY_TIME    (fc::days(15))
#define GAMEBANK_CONTENT_CONSTANT_HF0            (uint128_t(uint64_t(2000000000000ll)))
// note, if redefining these constants make sure calculate_claims doesn't overflow

// 5ccc e802 de5f
// int(expm1( log1p( 1 ) / BLOCKS_PER_YEAR ) * 2**GAMEBANK_APR_PERCENT_SHIFT_PER_BLOCK / 100000 + 0.5)
// we use 100000 here instead of 10000 because we end up creating an additional 9x for vesting
#define GAMEBANK_APR_PERCENT_MULTIPLY_PER_BLOCK          ( (uint64_t( 0x5ccc ) << 0x20) \
                                                        | (uint64_t( 0xe802 ) << 0x10) \
                                                        | (uint64_t( 0xde5f )        ) \
                                                        )
// chosen to be the maximal value such that GAMEBANK_APR_PERCENT_MULTIPLY_PER_BLOCK * 2**64 * 100000 < 2**128
#define GAMEBANK_APR_PERCENT_SHIFT_PER_BLOCK             87

#define GAMEBANK_APR_PERCENT_MULTIPLY_PER_ROUND          ( (uint64_t( 0x79cc ) << 0x20 ) \
                                                        | (uint64_t( 0xf5c7 ) << 0x10 ) \
                                                        | (uint64_t( 0x3480 )         ) \
                                                        )

#define GAMEBANK_APR_PERCENT_SHIFT_PER_ROUND             83

// We have different constants for hourly rewards
// i.e. hex(int(math.expm1( math.log1p( 1 ) / HOURS_PER_YEAR ) * 2**GAMEBANK_APR_PERCENT_SHIFT_PER_HOUR / 100000 + 0.5))
#define GAMEBANK_APR_PERCENT_MULTIPLY_PER_HOUR           ( (uint64_t( 0x6cc1 ) << 0x20) \
                                                        | (uint64_t( 0x39a1 ) << 0x10) \
                                                        | (uint64_t( 0x5cbd )        ) \
                                                        )

// chosen to be the maximal value such that GAMEBANK_APR_PERCENT_MULTIPLY_PER_HOUR * 2**64 * 100000 < 2**128
#define GAMEBANK_APR_PERCENT_SHIFT_PER_HOUR              77

// These constants add up to GRAPHENE_100_PERCENT.  Each GRAPHENE_1_PERCENT is equivalent to 1% per year APY
// *including the corresponding 9x vesting rewards*
#define GAMEBANK_CURATE_APR_PERCENT              3875
#define GAMEBANK_CONTENT_APR_PERCENT             3875
#define GAMEBANK_LIQUIDITY_APR_PERCENT            750
#define GAMEBANK_PRODUCER_APR_PERCENT             750
#define GAMEBANK_POW_APR_PERCENT                  750

#define GAMEBANK_MIN_PAYOUT_GBD                  (asset(20,GBD_SYMBOL))

#define GAMEBANK_GBD_STOP_PERCENT                (5*GAMEBANK_1_PERCENT ) // Stop printing GBD at 5% Market Cap
#define GAMEBANK_GBD_START_PERCENT               (2*GAMEBANK_1_PERCENT) // Start reducing printing of GBD at 2% Market Cap

#define GAMEBANK_MIN_ACCOUNT_NAME_LENGTH          3
#define GAMEBANK_MAX_ACCOUNT_NAME_LENGTH         16

#define GAMEBANK_MIN_PERMLINK_LENGTH             0
#define GAMEBANK_MAX_PERMLINK_LENGTH             256
#define GAMEBANK_MAX_WITNESS_URL_LENGTH          2048

#define GAMEBANK_MAX_SHARE_SUPPLY                int64_t(1000000000000000ll)
#define GAMEBANK_MAX_SATOSHIS                    int64_t(4611686018427387903ll)
#define GAMEBANK_MAX_SIG_CHECK_DEPTH             2

#define GAMEBANK_MIN_TRANSACTION_SIZE_LIMIT      1024
#define GAMEBANK_SECONDS_PER_YEAR                (uint64_t(60*60*24*365ll))

#define GAMEBANK_GBD_INTEREST_COMPOUND_INTERVAL_SEC  (60*60*24*30)
#define GAMEBANK_MAX_TRANSACTION_SIZE            (1024*64)
#define GAMEBANK_MIN_BLOCK_SIZE_LIMIT            (GAMEBANK_MAX_TRANSACTION_SIZE)
#define GAMEBANK_MAX_BLOCK_SIZE                  (GAMEBANK_MAX_TRANSACTION_SIZE*GAMEBANK_BLOCK_INTERVAL*2000)
#define GAMEBANK_SOFT_MAX_BLOCK_SIZE             (2*1024*1024)
#define GAMEBANK_MIN_BLOCK_SIZE                  115
#define GAMEBANK_BLOCKS_PER_HOUR                 (60*60/GAMEBANK_BLOCK_INTERVAL)
#define GAMEBANK_FEED_INTERVAL_BLOCKS            (GAMEBANK_BLOCKS_PER_HOUR)
#define GAMEBANK_FEED_HISTORY_WINDOW             (12*7) // 3.5 days
#define GAMEBANK_MAX_FEED_AGE_SECONDS            (60*60*24*7) // 7 days
#define GAMEBANK_MIN_FEEDS                       (GAMEBANK_MAX_WITNESSES/3) /// protects the network from conversions before price has been established
#define GAMEBANK_CONVERSION_DELAY                (fc::hours(GAMEBANK_FEED_HISTORY_WINDOW)) //3.5 day conversion

#define GAMEBANK_MIN_UNDO_HISTORY                10
#define GAMEBANK_MAX_UNDO_HISTORY                10000

#define GAMEBANK_MIN_TRANSACTION_EXPIRATION_LIMIT (GAMEBANK_BLOCK_INTERVAL * 5) // 5 transactions per block
#define GAMEBANK_BLOCKCHAIN_PRECISION            uint64_t( 1000 )

#define GAMEBANK_BLOCKCHAIN_PRECISION_DIGITS     3
#define GAMEBANK_MAX_INSTANCE_ID                 (uint64_t(-1)>>16)
/** NOTE: making this a power of 2 (say 2^15) would greatly accelerate fee calcs */
#define GAMEBANK_MAX_AUTHORITY_MEMBERSHIP        10
#define GAMEBANK_MAX_ASSET_WHITELIST_AUTHORITIES 10
#define GAMEBANK_MAX_URL_LENGTH                  127

#define GAMEBANK_IRREVERSIBLE_THRESHOLD          (75 * GAMEBANK_1_PERCENT)

#define GAMEBANK_VIRTUAL_SCHEDULE_LAP_LENGTH  ( fc::uint128(uint64_t(-1)) )
#define GAMEBANK_VIRTUAL_SCHEDULE_LAP_LENGTH2 ( fc::uint128::max_value() )

#define GAMEBANK_INITIAL_VOTE_POWER_RATE (40)
#define GAMEBANK_REDUCED_VOTE_POWER_RATE (10)

#define GAMEBANK_MAX_LIMIT_ORDER_EXPIRATION     (60*60*24*28) // 28 days
#define GAMEBANK_DELEGATION_RETURN_PERIOD                   (GAMEBANK_CASHOUT_WINDOW_SECONDS)
#define GAMEBANK_DELEGATION_RETURN_PERIOD_HF01                   (GAMEBANK_VOTE_REGENERATION_SECONDS * 2)

/**
 *  Reserved Account IDs with special meaning
 */
///@{
/// Represents the current witnesses
#define GAMEBANK_MINER_ACCOUNT                   "miners"
/// Represents the canonical account with NO authority (nobody can access funds in null account)
#define GAMEBANK_NULL_ACCOUNT                    "null"
/// Represents the canonical account with WILDCARD authority (anybody can access funds in temp account)
#define GAMEBANK_TEMP_ACCOUNT                    "temp"
/// Represents the canonical account for specifying you will vote for directly (as opposed to a proxy)
#define GAMEBANK_PROXY_TO_SELF_ACCOUNT           ""
/// Represents the canonical root post parent account
#define GAMEBANK_ROOT_POST_PARENT                (account_name_type())
///@}


