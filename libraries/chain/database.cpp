#include <gamebank/protocol/gamebank_operations.hpp>

#include <gamebank/chain/block_summary_object.hpp>
#include <gamebank/chain/compound.hpp>
#include <gamebank/chain/custom_operation_interpreter.hpp>
#include <gamebank/chain/database.hpp>
#include <gamebank/chain/database_exceptions.hpp>
#include <gamebank/chain/db_with.hpp>
#include <gamebank/chain/evaluator_registry.hpp>
#include <gamebank/chain/global_property_object.hpp>
#include <gamebank/chain/history_object.hpp>
#include <gamebank/chain/index.hpp>
#include <gamebank/chain/gamebank_evaluator.hpp>
#include <gamebank/chain/gamebank_objects.hpp>
#include <gamebank/chain/transaction_object.hpp>
#include <gamebank/chain/shared_db_merkle.hpp>
#include <gamebank/chain/operation_notification.hpp>
#include <gamebank/chain/witness_schedule.hpp>
#include <gamebank/chain/nonfungible_fund_object.hpp>
#include <gamebank/chain/nonfungible_fund_on_sale_object.hpp>

#include <gamebank/chain/util/asset.hpp>
#include <gamebank/chain/util/reward.hpp>
#include <gamebank/chain/util/uint256.hpp>
#include <gamebank/chain/util/reward.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/uint128.hpp>

#include <fc/container/deque.hpp>

#include <fc/io/fstream.hpp>

#include <boost/scope_exit.hpp>

#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>

namespace gamebank { namespace chain {

struct object_schema_repr
{
   std::pair< uint16_t, uint16_t > space_type;
   std::string type;
};

struct operation_schema_repr
{
   std::string id;
   std::string type;
};

struct db_schema
{
   std::map< std::string, std::string > types;
   std::vector< object_schema_repr > object_types;
   std::string operation_type;
   std::vector< operation_schema_repr > custom_operation_types;
};

} }

FC_REFLECT( gamebank::chain::object_schema_repr, (space_type)(type) )
FC_REFLECT( gamebank::chain::operation_schema_repr, (id)(type) )
FC_REFLECT( gamebank::chain::db_schema, (types)(object_types)(operation_type)(custom_operation_types) )

namespace gamebank { namespace chain {

using boost::container::flat_set;

struct reward_fund_context
{
   uint128_t   recent_claims = 0;
   asset       reward_balance = asset( 0, GBC_SYMBOL );
   share_type  gbc_awarded = 0;
};

class database_impl
{
   public:
      database_impl( database& self );

      database&                              _self;
      evaluator_registry< operation >        _evaluator_registry;
};

database_impl::database_impl( database& self )
   : _self(self), _evaluator_registry(self) {}

database::database()
   : _my( new database_impl(*this) )
{
   set_chain_id( GAMEBANK_CHAIN_ID_NAME );
}

database::~database()
{
   clear_pending();
}

void database::open( const open_args& args )
{
   try
   {
      init_schema();
      chainbase::database::open( args.shared_mem_dir, args.chainbase_flags, args.shared_file_size );

      initialize_indexes();
      initialize_evaluators();

      if( !find< dynamic_global_property_object >() )
         with_write_lock( [&]()
         {
            init_genesis( args.initial_supply );
         });

      _benchmark_dumper.set_enabled( args.benchmark_is_enabled );

      _block_log.open( args.data_dir / "block_log" );

      auto log_head = _block_log.head();

      // Rewind all undo state. This should return us to the state at the last irreversible block.
      with_write_lock( [&]()
      {
         undo_all();
         FC_ASSERT( revision() == head_block_num(), "Chainbase revision does not match head block num",
            ("rev", revision())("head_block", head_block_num()) );
         if (args.do_validate_invariants)
            validate_invariants();
      });

      if( head_block_num() )
      {
      	 //从block log读取当前最新入链的区块
         auto head_block = _block_log.read_block_by_num( head_block_num() );
         // This assertion should be caught and a reindex should occur
         //head_block_id()是从全局属性获取的
         //head_block->id()来自block log
         //如果两者不等，说明当前链的状态与block log不匹配，需要reindex!
         FC_ASSERT( head_block.valid() && head_block->id() == head_block_id(), "Chain state does not match block log. Please reindex blockchain." );

		 //如果匹配，这把从block log获得的head block作为fork DB的开始区块
         _fork_db.start_block( *head_block );
      }

      with_read_lock( [&]()
      {
         init_hardforks(); // Writes to local state, but reads from db
      });

      if (args.benchmark.first)
      {
         args.benchmark.second(0, get_abstract_index_cntr());
         auto last_block_num = _block_log.head()->block_num();
         args.benchmark.second(last_block_num, get_abstract_index_cntr());
      }

      _shared_file_full_threshold = args.shared_file_full_threshold;
      _shared_file_scale_rate = args.shared_file_scale_rate;
   }
   FC_CAPTURE_LOG_AND_RETHROW( (args.data_dir)(args.shared_mem_dir)(args.shared_file_size) )
}

uint32_t database::reindex( const open_args& args )
{
   reindex_notification note;

   BOOST_SCOPE_EXIT(this_,&note) {
      GAMEBANK_TRY_NOTIFY(this_->_post_reindex_signal, note);
   } BOOST_SCOPE_EXIT_END

   try
   {
      GAMEBANK_TRY_NOTIFY(_pre_reindex_signal, note);

      ilog( "Reindexing Blockchain" );
      wipe( args.data_dir, args.shared_mem_dir, false );
      open( args );
      _fork_db.reset();    // override effect of _fork_db.start_block() call in open()

      auto start = fc::time_point::now();
      GAMEBANK_ASSERT( _block_log.head(), block_log_exception, "No blocks in block log. Cannot reindex an empty chain." );

      ilog( "Replaying blocks..." );

      uint64_t skip_flags =
         skip_witness_signature |
         skip_transaction_signatures |
         skip_transaction_dupe_check |
         skip_tapos_check |
         skip_merkle_check |
         skip_witness_schedule_check |
         skip_authority_check |
         skip_validate | /// no need to validate operations
         skip_validate_invariants |
         skip_block_log;

      with_write_lock( [&]()
      {
         _block_log.set_locking( false );
         auto itr = _block_log.read_block( 0 );
         auto last_block_num = _block_log.head()->block_num();
         if( args.stop_replay_at > 0 && args.stop_replay_at < last_block_num )
            last_block_num = args.stop_replay_at;
         if( args.benchmark.first > 0 )
         {
            args.benchmark.second( 0, get_abstract_index_cntr() );
         }

         while( itr.first.block_num() != last_block_num )
         {
            auto cur_block_num = itr.first.block_num();
            if( cur_block_num % 100000 == 0 )
               std::cerr << "   " << double( cur_block_num * 100 ) / last_block_num << "%   " << cur_block_num << " of " << last_block_num <<
               "   (" << (get_free_memory() / (1024*1024)) << "M free)\n";
            apply_block( itr.first, skip_flags );

            if( (args.benchmark.first > 0) && (cur_block_num % args.benchmark.first == 0) )
               args.benchmark.second( cur_block_num, get_abstract_index_cntr() );
            itr = _block_log.read_block( itr.second );
         }

         apply_block( itr.first, skip_flags );
         note.last_block_number = itr.first.block_num();

         if( (args.benchmark.first > 0) && (note.last_block_number % args.benchmark.first == 0) )
            args.benchmark.second( note.last_block_number, get_abstract_index_cntr() );
         set_revision( head_block_num() );
         _block_log.set_locking( true );
      });

      if( _block_log.head()->block_num() )
         _fork_db.start_block( *_block_log.head() );

      auto end = fc::time_point::now();
      ilog( "Done reindexing, elapsed time: ${t} sec", ("t",double((end-start).count())/1000000.0 ) );

      note.reindex_success = true;

      return note.last_block_number;
   }
   FC_CAPTURE_AND_RETHROW( (args.data_dir)(args.shared_mem_dir) )

}

void database::wipe( const fc::path& data_dir, const fc::path& shared_mem_dir, bool include_blocks)
{
   close();
   chainbase::database::wipe( shared_mem_dir );
   if( include_blocks )
   {
      fc::remove_all( data_dir / "block_log" );
      fc::remove_all( data_dir / "block_log.index" );
   }
}

void database::close(bool rewind)
{
   try
   {
      // Since pop_block() will move tx's in the popped blocks into pending,
      // we have to clear_pending() after we're done popping to get a clean
      // DB state (issue #336).
      clear_pending();

      chainbase::database::flush();
      chainbase::database::close();

      _block_log.close();

      _fork_db.reset();
   }
   FC_CAPTURE_AND_RETHROW()
}

bool database::is_known_block( const block_id_type& id )const
{ try {
   return fetch_block_by_id( id ).valid();
} FC_CAPTURE_AND_RETHROW() }

/**
 * Only return true *if* the transaction has not expired or been invalidated. If this
 * method is called with a VERY old transaction we will return false, they should
 * query things by blocks if they are that old.
 */
 //存放在transaction_index中的交易是有效的
bool database::is_known_transaction( const transaction_id_type& id )const
{ try {
   const auto& trx_idx = get_index<transaction_index>().indices().get<by_trx_id>();
   return trx_idx.find( id ) != trx_idx.end();
} FC_CAPTURE_AND_RETHROW() }

block_id_type database::find_block_id_for_num( uint32_t block_num )const
{
   try
   {
      if( block_num == 0 )
         return block_id_type();

      // Reversible blocks are *usually* in the TAPOS buffer.  Since this
      // is the fastest check, we do it first.
      block_summary_id_type bsid = block_num & 0xFFFF;
      const block_summary_object* bs = find< block_summary_object, by_id >( bsid );
      if( bs != nullptr )
      {
         if( protocol::block_header::num_from_id(bs->block_id) == block_num )
            return bs->block_id;
      }

      // Next we query the block log.   Irreversible blocks are here.
      auto b = _block_log.read_block_by_num( block_num );
      if( b.valid() )
         return b->id();

      // Finally we query the fork DB.
      shared_ptr< fork_item > fitem = _fork_db.fetch_block_on_main_branch_by_number( block_num );
      if( fitem )
         return fitem->id;

      return block_id_type();
   }
   FC_CAPTURE_AND_RETHROW( (block_num) )
}

block_id_type database::get_block_id_for_num( uint32_t block_num )const
{
   block_id_type bid = find_block_id_for_num( block_num );
   FC_ASSERT( bid != block_id_type() );
   return bid;
}


optional<signed_block> database::fetch_block_by_id( const block_id_type& id )const
{ try {
//先从fork DB提取block
   auto b = _fork_db.fetch_block( id );
   if( !b )
   {
   //如果forkDB没有，则从block log提取
      auto tmp = _block_log.read_block_by_num( protocol::block_header::num_from_id( id ) );

      if( tmp && tmp->id() == id )
         return tmp;

      tmp.reset();
      return tmp;
   }

   return b->data;
} FC_CAPTURE_AND_RETHROW() }

optional<signed_block> database::fetch_block_by_number( uint32_t block_num )const
{ try {
   optional< signed_block > b;

   auto results = _fork_db.fetch_block_by_number( block_num );
   if( results.size() == 1 )
      b = results[0]->data;
   else
      b = _block_log.read_block_by_num( block_num );

   return b;
} FC_LOG_AND_RETHROW() }

//从transaction_index中提取交易，这个交易应该是有效的
const signed_transaction database::get_recent_transaction( const transaction_id_type& trx_id ) const
{ try {
   auto& index = get_index<transaction_index>().indices().get<by_trx_id>();
   auto itr = index.find(trx_id);
   FC_ASSERT(itr != index.end());
   signed_transaction trx;
   fc::raw::unpack_from_buffer( itr->packed_trx, trx );
   return trx;;
} FC_CAPTURE_AND_RETHROW() }

//获取head_of_fork分支从分支尾部到分叉点（共同祖先）倒叙的block id列表
std::vector< block_id_type > database::get_block_ids_on_fork( block_id_type head_of_fork ) const
{ try {
   pair<fork_database::branch_type, fork_database::branch_type> branches = _fork_db.fetch_branch_from(head_block_id(), head_of_fork);
   if( !((branches.first.back()->previous_id() == branches.second.back()->previous_id())) )
   {
   //两个分支没有共同祖先
      edump( (head_of_fork)
             (head_block_id())
             (branches.first.size())
             (branches.second.size()) );
      assert(branches.first.back()->previous_id() == branches.second.back()->previous_id());
   }
/*...有共同祖先...*/

   //存放head_of_fork分支的所有区块id 和 共同祖先
   std::vector< block_id_type > result;
   for( const item_ptr& fork_block : branches.second )
      result.emplace_back(fork_block->id);
   result.emplace_back(branches.first.back()->previous_id());
   return result;
} FC_CAPTURE_AND_RETHROW() }

chain_id_type database::get_chain_id() const
{
   return gamebank_chain_id;
}

void database::set_chain_id( const std::string& _chain_id_name )
{
   gamebank_chain_id = generate_chain_id( _chain_id_name );
}

void database::foreach_block(std::function<bool(const signed_block_header&, const signed_block&)> processor) const
{
   if(!_block_log.head())
      return;

   auto itr = _block_log.read_block( 0 );
   auto last_block_num = _block_log.head()->block_num();
   signed_block_header previousBlockHeader = itr.first;
   while( itr.first.block_num() != last_block_num )
   {
      const signed_block& b = itr.first;
      if(processor(previousBlockHeader, b) == false)
         return;

      previousBlockHeader = b;
      itr = _block_log.read_block( itr.second );
   }

   processor(previousBlockHeader, itr.first);
}

void database::foreach_tx(std::function<bool(const signed_block_header&, const signed_block&,
   const signed_transaction&, uint32_t)> processor) const
{
   foreach_block([&processor](const signed_block_header& prevBlockHeader, const signed_block& block) -> bool
   {
      uint32_t txInBlock = 0;
      for( const auto& trx : block.transactions )
      {
         if(processor(prevBlockHeader, block, trx, txInBlock) == false)
            return false;
         ++txInBlock;
      }

      return true;
   }
   );
}

void database::foreach_operation(std::function<bool(const signed_block_header&,const signed_block&,
   const signed_transaction&, uint32_t, const operation&, uint16_t)> processor) const
{
   foreach_tx([&processor](const signed_block_header& prevBlockHeader, const signed_block& block,
      const signed_transaction& tx, uint32_t txInBlock) -> bool
   {
      uint16_t opInTx = 0;
      for(const auto& op : tx.operations)
      {
         if(processor(prevBlockHeader, block, tx, txInBlock, op, opInTx) == false)
            return false;
         ++opInTx;
      }

      return true;
   }
   );
}


const witness_object& database::get_witness( const account_name_type& name ) const
{ try {
   return get< witness_object, by_name >( name );
} FC_CAPTURE_AND_RETHROW( (name) ) }

const witness_object* database::find_witness( const account_name_type& name ) const
{
   return find< witness_object, by_name >( name );
}

const account_object& database::get_account( const account_name_type& name )const
{ try {
   return get< account_object, by_name >( name );
} FC_CAPTURE_AND_RETHROW( (name) ) }

const account_object* database::find_account( const account_name_type& name )const
{
   return find< account_object, by_name >( name );
}

const crowdfunding_object& database::get_crowdfunding( const account_name_type& originator, const shared_string& permlink )const
{ try {
        return get< crowdfunding_object, by_permlink >(boost::make_tuple(originator, permlink));
} FC_CAPTURE_AND_RETHROW( (originator)(permlink) ) }


const crowdfunding_object* database::find_crowdfunding( const account_name_type& originator, const shared_string& permlink )const
{
    return find< crowdfunding_object, by_permlink >(boost::make_tuple(originator, permlink));
}

const comment_object& database::get_comment( const account_name_type& author, const shared_string& permlink )const
{ try {
   return get< comment_object, by_permlink >( boost::make_tuple( author, permlink ) );
} FC_CAPTURE_AND_RETHROW( (author)(permlink) ) }

const comment_object* database::find_comment( const account_name_type& author, const shared_string& permlink )const
{
   return find< comment_object, by_permlink >( boost::make_tuple( author, permlink ) );
}

#ifndef ENABLE_STD_ALLOCATOR
const crowdfunding_object& database::get_crowdfunding(const account_name_type& originator, const string& permlink)const
{ try {
   return get< crowdfunding_object, by_permlink >(boost::make_tuple(originator, permlink));
} FC_CAPTURE_AND_RETHROW((originator)(permlink)) }

const crowdfunding_object* database::find_crowdfunding(const account_name_type& originator, const string& permlink)const
{
    return find< crowdfunding_object, by_permlink >(boost::make_tuple(originator, permlink));
}

const comment_object& database::get_comment( const account_name_type& author, const string& permlink )const
{ try {
   return get< comment_object, by_permlink >( boost::make_tuple( author, permlink) );
} FC_CAPTURE_AND_RETHROW( (author)(permlink) ) }

const comment_object* database::find_comment( const account_name_type& author, const string& permlink )const
{
   return find< comment_object, by_permlink >( boost::make_tuple( author, permlink ) );
}
#endif

const escrow_object& database::get_escrow( const account_name_type& name, uint32_t escrow_id )const
{ try {
   return get< escrow_object, by_from_id >( boost::make_tuple( name, escrow_id ) );
} FC_CAPTURE_AND_RETHROW( (name)(escrow_id) ) }

const escrow_object* database::find_escrow( const account_name_type& name, uint32_t escrow_id )const
{
   return find< escrow_object, by_from_id >( boost::make_tuple( name, escrow_id ) );
}

const limit_order_object& database::get_limit_order( const account_name_type& name, uint32_t orderid )const
{ try {
   return get< limit_order_object, by_account >( boost::make_tuple( name, orderid ) );
} FC_CAPTURE_AND_RETHROW( (name)(orderid) ) }

const limit_order_object* database::find_limit_order( const account_name_type& name, uint32_t orderid )const
{
   return find< limit_order_object, by_account >( boost::make_tuple( name, orderid ) );
}

const savings_withdraw_object& database::get_savings_withdraw( const account_name_type& owner, uint32_t request_id )const
{ try {
   return get< savings_withdraw_object, by_from_rid >( boost::make_tuple( owner, request_id ) );
} FC_CAPTURE_AND_RETHROW( (owner)(request_id) ) }

const savings_withdraw_object* database::find_savings_withdraw( const account_name_type& owner, uint32_t request_id )const
{
   return find< savings_withdraw_object, by_from_rid >( boost::make_tuple( owner, request_id ) );
}

const dynamic_global_property_object&database::get_dynamic_global_properties() const
{ try {
   return get< dynamic_global_property_object >();
} FC_CAPTURE_AND_RETHROW() }

const node_property_object& database::get_node_properties() const
{
   return _node_property_object;
}

const feed_history_object& database::get_feed_history()const
{ try {
   return get< feed_history_object >();
} FC_CAPTURE_AND_RETHROW() }

const witness_schedule_object& database::get_witness_schedule_object()const
{ try {
   return get< witness_schedule_object >();
} FC_CAPTURE_AND_RETHROW() }

const hardfork_property_object& database::get_hardfork_property_object()const
{ try {
   return get< hardfork_property_object >();
} FC_CAPTURE_AND_RETHROW() }

const time_point_sec database::calculate_discussion_payout_time( const comment_object& comment )const
{
      return comment.cashout_time;
}

const reward_fund_object& database::get_reward_fund( const comment_object& c ) const
{
   return get< reward_fund_object, by_name >( GAMEBANK_POST_REWARD_FUND_NAME );
}

asset database::get_effective_vesting_shares( const account_object& account, asset_symbol_type vested_symbol )const
{
   if( vested_symbol == GBS_SYMBOL )
      return account.vesting_shares - account.delegated_vesting_shares + account.received_vesting_shares;

   FC_ASSERT( false, "Invalid symbol" );
}

uint32_t database::witness_participation_rate()const
{
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   return uint64_t(GAMEBANK_100_PERCENT) * dpo.recent_slots_filled.popcount() / 128;
}

void database::add_checkpoints( const flat_map< uint32_t, block_id_type >& checkpts )
{
   for( const auto& i : checkpts )
      _checkpoints[i.first] = i.second;
}

bool database::before_last_checkpoint()const
{
   return (_checkpoints.size() > 0) && (_checkpoints.rbegin()->first >= head_block_num());
}

/**
 * Push block "may fail" in which case every partial change is unwound.  After
 * push block is successful the block is appended to the chain database on disk.
 *
 * @return true if we switched forks as a result of this push.
 */
 
 //把区块添加到链！
 //_generate_block -> push_block-> _push_block -> apply_block
bool database::push_block(const signed_block& new_block, uint32_t skip)
{
   //fc::time_point begin_time = fc::time_point::now();

   bool result;
   detail::with_skip_flags( *this, skip, [&]()
   {
   //lambda
      //@see without_pending_transactions
      //首先备份_pending_tx
      //然后调用lambda
      //最后恢复_pending_tx,并检验其中的交易和poped_tx中的交易，未过期且有效的重新push到db
      //----这么做是因为_push_block过程中...
      detail::without_pending_transactions( *this, std::move(_pending_tx), [&]()
      {
         try
         {
         	// 调用实际的推块方法database::_push_block
            result = _push_block(new_block);
         }
         FC_CAPTURE_AND_RETHROW( (new_block) )

         check_free_memory( false, new_block.block_num() );
      });
   });

   //fc::time_point end_time = fc::time_point::now();
   //fc::microseconds dt = end_time - begin_time;
   //if( ( new_block.block_num() % 10000 ) == 0 )
   //   ilog( "push_block ${b} took ${t} microseconds", ("b", new_block.block_num())("t", dt.count()) );
   return result;
}

void database::confirm_block(const signed_block& blk, uint32_t skip)
{
    try {
        const witness_object& witness = validate_block_header( skip, blk );
        update_confirm_witness(witness, blk);
    }
    FC_CAPTURE_LOG_AND_RETHROW((blk.block_num()))
}

//height:新生成的区块或者新收到的区块的numer（高度）
void database::_maybe_warn_multiple_production( uint32_t height )const
{
   auto blocks = _fork_db.fetch_block_by_number( height );
   if( blocks.size() > 1 )
   {
   //_fork_db中有多个区块编号相同的区块
      vector< std::pair< account_name_type, fc::time_point_sec > > witness_time_pairs;
      for( const auto& b : blocks )
      {
      //<区块生成者，区块生成时间>
         witness_time_pairs.push_back( std::make_pair( b->data.witness, b->data.timestamp ) );
      }

      ilog( "Encountered block num collision at block ${n} due to a fork, witnesses are: ${w}", ("n", height)("w", witness_time_pairs) );
   }
   return;
}

//把区块添加到链的实际处理函数
//_push_block->apply_block
//new_block:新生成的区块,或从远端接收到的区块
//1 首先把新区块push到forkDB
//2 判断是否有更长链存在
//3 如果有，则把短链上从分叉点到head的所有区块回滚，每个回滚的区块中的所有交易保存到poped_tx
//4 把长链上每一个从分叉点到new_head的区块调用apply_block
//5 如果不存在更长链，则直接把调用apply_block(new_block)
bool database::_push_block(const signed_block& new_block)
{ try {
   #ifdef IS_TEST_NET
   FC_ASSERT(new_block.block_num() < TESTNET_BLOCK_LIMIT, "Testnet block limit exceeded");
   #endif /// IS_TEST_NET

   uint32_t skip = get_node_properties().skip_flags;
   //uint32_t skip_undo_db = skip & skip_undo_block;

  //区块分叉问题
   if( !(skip&skip_fork_db) )
   {
   	  //首先push到forkDB
      shared_ptr<fork_item> new_head = _fork_db.push_block(new_block);
      _maybe_warn_multiple_production( new_head->num );

      //If the head block from the longest chain does not build off of the current head, we need to switch forks.
      //新区块无法直接接入当前链
      if( new_head->data.previous != head_block_id() )
      {
      	 //不能正常直接入链，可能出现分叉
         //If the newly pushed block is the same height as head, we get head back in new_head
         //Only switch forks if new_head is actually higher than head
		 // 有比当前链更长的,才处理分叉
         if( new_head->data.block_num() > head_block_num() )
         {
            //如果新的区块高度比本地维护的链的高度更高，说明有更长的链存在
            // wlog( "Switching to fork: ${id}", ("id",new_head->data.id()) );
			// 找出2条分叉路径:
			// fork_node -> new_head (这条更长,因为new_head->data.block_num() > head_block_num())
			// fork_node -> head_block_id
            auto branches = _fork_db.fetch_branch_from(new_head->data.id(), head_block_id());

            // pop blocks until we hit the forked block
			// 回退到分叉点
            while( head_block_id() != branches.second.back()->data.previous )
               pop_block();	//这将把回滚的block中所有交易保存到poped_tx

            // push all blocks on the new fork
			// 把fork_node -> new_head的区块,重新计算一次
            for( auto ritr = branches.first.rbegin(); ritr != branches.first.rend(); ++ritr )
            {
                // ilog( "pushing blocks from fork ${n} ${id}", ("n",(*ritr)->data.block_num())("id",(*ritr)->data.id()) );
                optional<fc::exception> except;
                try
                {
                   auto session = start_undo_session();
				   //将new head分支上的区块入链
				   //从分叉点的后一个区块开始压入
                   apply_block( (*ritr)->data, skip );
                   session.push();
                }
                catch ( const fc::exception& e ) { except = e; }
                if( except )
                {
				   // 处理失败,表示fork_node -> new_head这条链有问题
				   // 删除这条链在_fork_db里的数据
                   // wlog( "exception thrown while switching forks ${e}", ("e",except->to_detail_string() ) );
                   // remove the rest of branches.first from the fork_db, those blocks are invalid
                   while( ritr != branches.first.rend() )
                   {
                      _fork_db.remove( (*ritr)->data.id() );
                      ++ritr;
                   }
				   // 重置为head_block_id
                   _fork_db.set_head( branches.second.front() );

                   // pop all blocks from the bad fork
				   // 回退到分叉点
                   while( head_block_id() != branches.second.back()->data.previous )
                      pop_block();

                   // restore all blocks from the good fork
				   // 重新计算,回到head_block_id
                   for( auto ritr = branches.second.rbegin(); ritr != branches.second.rend(); ++ritr )
                   {
                      auto session = start_undo_session();
                      apply_block( (*ritr)->data, skip );
                      session.push();
                   }
                   throw *except;
                }
            }
            return true;
         }
         else
            return false;
      }
   }//---区块分叉问题end

   try
   {
   	  // 将新区块添加到链上

      auto session = start_undo_session();
      apply_block(new_block, skip);
      session.push();
   }
   catch( const fc::exception& e )
   {
      elog("Failed to push new block:\n${e}", ("e", e.to_detail_string()));
      _fork_db.remove(new_block.id());
      throw;
   }

   return false;
} FC_CAPTURE_AND_RETHROW() }

/**
 * Attempts to push the transaction into the pending queue
 *
 * When called to push a locally generated transaction, set the skip_block_size_check bit on the skip argument. This
 * will allow the transaction to be pushed even if it causes the pending block size to exceed the maximum block size.
 * 本地生成的交易（即不是从远端节点接收的?）可以跳过block size检查
 * Although the transaction will probably not propagate further now, as the peers are likely to have their pending
 * queues full as well, it will be kept in the queue to be propagated later when a new block flushes out the pending
 * queues.
 */
 //push_transaction -> _push_transaction -> _apply_transaction
void database::push_transaction( const signed_transaction& trx, uint32_t skip )
{
   try
   {
      try
      {
         FC_ASSERT( fc::raw::pack_size(trx) <= (get_dynamic_global_properties().maximum_block_size - 256) );
         set_producing( true );
         detail::with_skip_flags( *this, skip,
            [&]()
            {
               _push_transaction( trx );
            });
         set_producing( false );
      }
      catch( ... )
      {
         set_producing( false );
         throw;
      }
   }
   FC_CAPTURE_AND_RETHROW( (trx) )
}

//1 执行_apply_transaction
//2 新收到的交易放入_pending_tx，当生产新区块将其打包加入新区块
void database::_push_transaction( const signed_transaction& trx )
{
   // If this is the first transaction pushed after applying a block, start a new undo session.
   // This allows us to quickly rewind to the clean state of the head block, in case a new block arrives.
   if( !_pending_tx_session.valid() )
      _pending_tx_session = start_undo_session();

   // Create a temporary undo session as a child of _pending_tx_session.
   // The temporary session will be discarded by the destructor if
   // _apply_transaction fails.  If we make it to merge(), we
   // apply the changes.

   auto temp_session = start_undo_session();
   _apply_transaction( trx );
   _pending_tx.push_back( trx );

   //do nothing
   notify_changed_objects();
   // The transaction applied successfully. Merge its changes into the pending block session.
   temp_session.squash();
}

signed_block database::generate_block(
   fc::time_point_sec when,
   const account_name_type& witness_owner,
   const fc::ecc::private_key& block_signing_private_key,
   uint32_t skip /* = 0 */
   )
{
   signed_block result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      try
      {
         result = _generate_block( when, witness_owner, block_signing_private_key );
      }
      FC_CAPTURE_AND_RETHROW( (witness_owner) )
   });
   return result;
}
   
//1 验证当前见证人是否是区c块生产者
//2 验证key是否是区块生产者的signing_key
//3 处理区块中所有交易（是否过期，尺寸超过区块限制， _apply_transaction）
//4 配置block_header
//5 hardfork
//6 给区块签名
//7 检验区块大小是否超限
//8 调用push_block
signed_block database::_generate_block(
   fc::time_point_sec when,
   const account_name_type& witness_owner,
   const fc::ecc::private_key& block_signing_private_key
   )
{
   uint32_t skip = get_node_properties().skip_flags;
   //获取区块生产时间槽
   uint32_t slot_num = get_slot_at_time( when );
   FC_ASSERT( slot_num > 0 );
   //获取指区块生产调度证人
   string scheduled_witness = get_scheduled_witness( slot_num );	
   FC_ASSERT( scheduled_witness == witness_owner );

   //获取witness_object
   const auto& witness_obj = get_witness( witness_owner );

   if( !(skip & skip_witness_signature) )
      FC_ASSERT( witness_obj.signing_key == block_signing_private_key.get_public_key() );

   static const size_t max_block_header_size = fc::raw::pack_size( signed_block_header() ) + 4;
   auto maximum_block_size = get_dynamic_global_properties().maximum_block_size; //GAMEBANK_MAX_BLOCK_SIZE;
   size_t total_block_size = max_block_header_size;

   //将要推到chain链的区块
   signed_block pending_block;

   //
   // The following code throws away existing pending_tx_session and
   // rebuilds it by re-applying pending transactions.
   //
   // This rebuild is necessary because pending transactions' validity
   // and semantics may have changed since they were received, because
   // time-based semantics are evaluated based on the current block
   // time.  These changes can only be reflected in the database when
   // the value of the "when" variable is known, which means we need to
   // re-apply pending transactions in this method.
   //
   _pending_tx_session.reset();
   _pending_tx_session = start_undo_session();

   uint64_t postponed_tx_count = 0;		//因交易尺寸过大而未添加到区块的交易数

   // pop pending state (reset to head block state)
   //循环处理_pending_tx队列中的所有交易, 且只处理未过期的交易并添加到pending_block
   //_pending_tx 由@see _push_transaction产生，即提交给见证人处理的交易
   for( const signed_transaction& tx : _pending_tx )
   {
      // Only include transactions that have not expired yet for currently generating block,
      // this should clear problem transactions and allow block production to continue

      //判断_pending_tx中的交易是否过期
      if( tx.expiration < when )
         continue;

      uint64_t new_total_size = total_block_size + fc::raw::pack_size( tx );

      // postpone transaction if it would make block too big
      //判断交易尺寸大小
      if( new_total_size >= maximum_block_size )
      {
         postponed_tx_count++;
         continue;
      }

      try
      {
         auto temp_session = start_undo_session();
         _apply_transaction( tx );
         temp_session.squash();

		 //更新当前区块大小
         total_block_size += fc::raw::pack_size( tx );
		 //打包交易: 把交易从_pending_tx放入区块
         pending_block.transactions.push_back( tx );
      }
      catch ( const fc::exception& e )
      {
         // Do nothing, transaction will not be re-applied
         //wlog( "Transaction was not processed while generating block due to ${e}", ("e", e) );
         //wlog( "The transaction was ${t}", ("t", tx) );
      }
   }//----tx loop end
   
   if( postponed_tx_count > 0 )
   {
      wlog( "Postponed ${n} transactions due to block size limit", ("n", postponed_tx_count) );
   }

   _pending_tx_session.reset();

   // We have temporarily broken the invariant that
   // _pending_tx_session is the result of applying _pending_tx, as
   // _pending_tx now consists of the set of postponed transactions.
   // However, the push_block() call below will re-create the
   // _pending_tx_session.

   pending_block.previous = head_block_id();		// 从全局属性获取的当前head id
   pending_block.timestamp = when;					//添加时间戳
   pending_block.transaction_merkle_root = pending_block.calculate_merkle_root();	//添加交易树根
   pending_block.witness = witness_owner;			//更新证人id

      const auto& witness = get_witness( witness_owner );

      if( witness.running_version != GAMEBANK_BLOCKCHAIN_VERSION )
         pending_block.extensions.insert( block_header_extensions( GAMEBANK_BLOCKCHAIN_VERSION ) );

      const auto& hfp = get_hardfork_property_object();

      if( hfp.current_hardfork_version < GAMEBANK_BLOCKCHAIN_VERSION // Binary is newer hardfork than has been applied
         && ( witness.hardfork_version_vote != _hardfork_versions[ hfp.last_hardfork + 1 ] || witness.hardfork_time_vote != _hardfork_times[ hfp.last_hardfork + 1 ] ) ) // Witness vote does not match binary configuration
      {
         // Make vote match binary configuration
         pending_block.extensions.insert( block_header_extensions( hardfork_version_vote( _hardfork_versions[ hfp.last_hardfork + 1 ], _hardfork_times[ hfp.last_hardfork + 1 ] ) ) );
      }
      else if( hfp.current_hardfork_version == GAMEBANK_BLOCKCHAIN_VERSION // Binary does not know of a new hardfork
         && witness.hardfork_version_vote > GAMEBANK_BLOCKCHAIN_VERSION ) // Voting for hardfork in the future, that we do not know of...
      {
         // Make vote match binary configuration. This is vote to not apply the new hardfork.
         pending_block.extensions.insert( block_header_extensions( hardfork_version_vote( _hardfork_versions[ hfp.last_hardfork ], _hardfork_times[ hfp.last_hardfork ] ) ) );
      }


   //区块签名
   if( !(skip & skip_witness_signature) )
      pending_block.sign( block_signing_private_key );

   // TODO:  Move this to _push_block() so session is restored.

   //检查区块大小
   if( !(skip & skip_block_size_check) )
   {
      FC_ASSERT( fc::raw::pack_size(pending_block) <= GAMEBANK_MAX_BLOCK_SIZE );
   }

   //调用推块方法，最后写入db
   push_block( pending_block, skip );

   return pending_block;
}

/**
 * Removes the most recent block from the database and
 * undoes any changes it made.
 */

//提取当前head block，并将head block中的交易保存到_popped_tx
void database::pop_block()
{
   try
   {
      _pending_tx_session.reset();
      auto head_id = head_block_id();

      /// save the head block so we can recover its transactions
      //从fork_db或block log提取当前head block
      optional<signed_block> head_block = fetch_block_by_id( head_id );
      GAMEBANK_ASSERT( head_block.valid(), pop_empty_chain, "there are no blocks to pop" );

      _fork_db.pop_block();//改变了fork_db的head
      undo();
	  //回滚的区块中所有交易保存到_popped_tx
      _popped_tx.insert( _popped_tx.begin(), head_block->transactions.begin(), head_block->transactions.end() );

   }
   FC_CAPTURE_AND_RETHROW()
}

void database::clear_pending()
{
   try
   {
      assert( (_pending_tx.size() == 0) || _pending_tx_session.valid() );
      _pending_tx.clear();
      _pending_tx_session.reset();
   }
   FC_CAPTURE_AND_RETHROW()
}

inline const void database::push_virtual_operation( const operation& op, bool force )
{
   /*
   if( !force )
   {
      #if defined( IS_LOW_MEM ) && ! defined( IS_TEST_NET )
      return;
      #endif
   }
   */

   FC_ASSERT( is_virtual_operation( op ) );
   operation_notification note(op);
   ++_current_virtual_op;
   note.virtual_op = _current_virtual_op;
   //emit the _pre_apply_operation_signal
   notify_pre_apply_operation( note );
   //emit the _post_apply_operation_signal
   notify_post_apply_operation( note );
}

void database::notify_pre_apply_operation( operation_notification& note )
{
   note.trx_id       = _current_trx_id;
   note.block        = _current_block_num;
   note.trx_in_block = _current_trx_in_block;
   note.op_in_trx    = _current_op_in_trx;
   //emit the signal
   GAMEBANK_TRY_NOTIFY( _pre_apply_operation_signal, note )
}

void database::notify_post_apply_operation( const operation_notification& note )
{
   GAMEBANK_TRY_NOTIFY( _post_apply_operation_signal, note )
}

void database::notify_pre_apply_block( const block_notification& note )
{
   GAMEBANK_TRY_NOTIFY( _pre_apply_block_signal, note )
}

void database::notify_irreversible_block( uint32_t block_num )
{
   GAMEBANK_TRY_NOTIFY( _on_irreversible_block, block_num )
}

void database::notify_post_apply_block( const block_notification& note )
{
   GAMEBANK_TRY_NOTIFY( _post_apply_block_signal, note )
}

void database::notify_pre_apply_transaction( const transaction_notification& note )
{
   GAMEBANK_TRY_NOTIFY( _pre_apply_transaction_signal, note )
}

void database::notify_post_apply_transaction( const transaction_notification& note )
{
   GAMEBANK_TRY_NOTIFY( _post_apply_transaction_signal, note )
}

//根据指定的时间槽的获取区块生产证人
account_name_type database::get_scheduled_witness( uint32_t slot_num )const
{
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   const witness_schedule_object& wso = get_witness_schedule_object();
   uint64_t current_aslot = dpo.current_aslot + slot_num;
   //从洗牌后的集合获取
   return wso.current_shuffled_witnesses[ current_aslot % wso.num_scheduled_witnesses ];
}

//从当前区块开始，未来第slot_num个区块的生成时间
fc::time_point_sec database::get_slot_time(uint32_t slot_num)const
{
   if( slot_num == 0 )
      return fc::time_point_sec();

   //区块生成间隔：3秒
   auto interval = GAMEBANK_BLOCK_INTERVAL;
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();

   if( head_block_num() == 0 )
   {
      // n.b. first block is at genesis_time plus one block interval
      //第一个区块的生成时间 = 创世区块时间+间隔
      fc::time_point_sec genesis_time = dpo.time;
      return genesis_time + slot_num * interval;
   }

   //获取前一个区块生产时间槽时刻
   int64_t head_block_abs_slot = head_block_time().sec_since_epoch() / interval;
   fc::time_point_sec head_slot_time( head_block_abs_slot * interval );

   // "slot 0" is head_slot_time
   // "slot 1" is head_slot_time,
   //   plus maint interval if head block is a maint block
   //   plus block interval if head block is not a maint block
   return head_slot_time + (slot_num * interval);
}

//获取指定时间属于未来哪个[时间槽]
uint32_t database::get_slot_at_time(fc::time_point_sec when)const
{
   //获取下一个区块生产时间
   fc::time_point_sec first_slot_time = get_slot_time( 1 );
   if( when < first_slot_time )
      return 0;
   return (when - first_slot_time).to_seconds() / GAMEBANK_BLOCK_INTERVAL + 1;
}

/**
 *  Converts GBC into gbd and adds it to to_account while reducing the GBC supply
 *  by GBC and increasing the gbd supply by the specified amount.
 */
 //apply_block ---> process_comment_cashout ---> cashout_comment_helper
 //返回值：<因收益而新分配的GBD资产，因收益而新分配的GBC资产>
 //<gbd, GAMEBANK_SYMBOL>
std::pair< asset, asset > database::create_gbd( const account_object& to_account, asset gbc, bool to_reward_balance )
{
   std::pair< asset, asset > assets( asset( 0, GBD_SYMBOL ), asset( 0, GBC_SYMBOL ) );

   try
   {
      if( gbc.amount == 0 )
         return assets;

      const auto& median_price = get_feed_history().current_median_history;
      const auto& gpo = get_dynamic_global_properties();

      if( !median_price.is_null() )
      {
      //存在喂价
         //当前债务所有权比率决定gbd_print_rate, gbd_print_rate决定分配的gbd/gamebank各自所占比率
         //gbd_print_rate能够在Ratio过高时抑制SBD的发行
         //GBD结算的GBD数量
         auto to_gbd = ( gpo.gbd_print_rate * gbc.amount ) / GAMEBANK_100_PERCENT;
         //GBC结算的GBC数量
         auto to_gbc = gbc.amount - to_gbd;

		 //to_gbd表示的是GBD数量
		 //gbd在这里表示to_gbd数量对应的GBD资产(asset)
		 //（to_gbd * median_price.base.amount.value）/median_price.quote.amount.value
		 //gbd asset单位是median_price.base.amount.value即GBD_SYMBOL
         auto gbd = asset( to_gbd, GBC_SYMBOL ) * median_price;

         if( to_reward_balance )
         {
            adjust_reward_balance( to_account, gbd );
            adjust_reward_balance( to_account, asset( to_gbc, GBC_SYMBOL ) );
         }
         else
         { 
            //账号GBD收益（asset）
            adjust_balance( to_account, gbd );
			//账户的GBC收益，第二参数必须是asset（to_gbc仅仅是数量）
            adjust_balance( to_account, asset( to_gbc, GBC_SYMBOL ) );
         }
		 //较少current_supply和virtual_supply
         adjust_supply( asset( -to_gbd, GBC_SYMBOL ) );
		 //新增的GBD(asset)供应，修改curr_gbd_supply
         adjust_supply( gbd );
         assets.first = gbd;								//GBD
         assets.second = asset( to_gbc, GBC_SYMBOL ); 		//GBC
      }
      else
      {
      //不存在喂价，收益全部以GBC的方式给账户
         adjust_balance( to_account, gbc );
         assets.second = gbc;
      }
   }
   FC_CAPTURE_LOG_AND_RETHROW( (to_account.name)(gbc) )

   return assets;
}

/**
 * @param to_account - the account to receive the new vesting shares
 * @param liquid     - GBC to be converted to vesting shares
 */
 //1 power up时会调用此函数@see transfer_to_vesting_evaluator::do_apply
 //用户使用GBC购买GBS
 //to_account有可能是支付GBC的账户
 //liquid: 用户使用多少GBC转换为GBS

//2 process_funds会调用，用于奖励生产新区块的见证人
//to_account: 新区块见证人账号
//liquid：witness_rewards.见证人分配到的新增gamebank份额

//3 cashout_comment_helper会调用，奖励给post作者的SP份额
//liquid：奖励给作者的GBC计价数量（会根据汇率转为GBS）
asset database::create_vesting( const account_object& to_account, asset liquid, bool to_reward_balance )
{
   try
   {
      auto calculate_new_vesting = [ liquid ] ( price vesting_share_price ) -> asset
         {
         /**
          *  The ratio of total_vesting_shares / total_vesting_fund_gbc should not
          *  change as the result of the user adding funds
          *
          *  V / C  = (V+Vn) / (C+Cn)
          *
          *  Simplifies to Vn = (V * Cn ) / C
          *
          *  If Cn equals o.amount, then we must solve for Vn to know how many new vesting shares
          *  the user should receive.
          *
          *  128 bit math is requred due to multiplying of 64 bit numbers. This is done in asset and price.
          */
          //vesting_share_price: gbc to GBS
         asset new_vesting = liquid * ( vesting_share_price );	
         return new_vesting;
         };

 		//必须是GBC to GBS
      FC_ASSERT( liquid.symbol == GBC_SYMBOL );
      // ^ A novelty, needed but risky in case someone managed to slip GBD/GBC here in blockchain history.
      // Get share price.
      const auto& cprops = get_dynamic_global_properties();
	  //POWER UP时to_reward_balance为false
	  //to_reward_balance默认为false

	  //获取GBC to GBS 汇率
	  //对作者post奖励，如果是GBC_HARDFORK_0_17__659,则取get_reward_vesting_share_price
      price vesting_share_price = to_reward_balance ? cprops.get_reward_vesting_share_price() : cprops.get_vesting_share_price();

	  // Calculate new vesting from provided liquid using share price.
      //经过汇率计算后得出可获得的VEST
      asset new_vesting = calculate_new_vesting( vesting_share_price );

	  // Add new vesting to owner's balance.
      //新增GBS到账户存款
      if( to_reward_balance )
         adjust_reward_balance( to_account, liquid, new_vesting );
      else
         adjust_balance( to_account, new_vesting );
      // Update global vesting pool numbers.
      //更新全局gamebank和vest状态
      modify( cprops, [&]( dynamic_global_property_object& props )
      {
         if( to_reward_balance )
         {
            props.pending_rewarded_vesting_shares += new_vesting;//GBS
            props.pending_rewarded_vesting_gbc += liquid;	//GBC
         }
         else
         {
            props.total_vesting_fund_gbc += liquid;//total_vesting_fund_gbc代表SP持有者奖励池.这里需要增加
            props.total_vesting_shares += new_vesting;//区块链总的GBS
         }
      } );
      // Update witness voting numbers.
      if( !to_reward_balance )
         adjust_proxied_witness_votes( to_account, new_vesting.amount );

      return new_vesting;
   }
   FC_CAPTURE_AND_RETHROW( (to_account.name)(liquid) )
}

fc::sha256 database::get_pow_target()const
{
   const auto& dgp = get_dynamic_global_properties();
   fc::sha256 target;
   target._hash[0] = -1;
   target._hash[1] = -1;
   target._hash[2] = -1;
   target._hash[3] = -1;
   target = target >> ((dgp.num_pow_witnesses/4)+4);
   return target;
}

uint32_t database::get_pow_summary_target()const
{
   const dynamic_global_property_object& dgp = get_dynamic_global_properties();
   if( dgp.num_pow_witnesses >= 1004 )
      return 0;

   return (0xFE00 - 0x0040 * dgp.num_pow_witnesses ) << 0x10;
}

void database::adjust_proxied_witness_votes( const account_object& a,
                                   const std::array< share_type, GAMEBANK_MAX_PROXY_RECURSION_DEPTH+1 >& delta,
                                   int depth )
{
   if( a.proxy != GAMEBANK_PROXY_TO_SELF_ACCOUNT )
   {
      /// nested proxies are not supported, vote will not propagate
      if( depth >= GAMEBANK_MAX_PROXY_RECURSION_DEPTH )
         return;

      const auto& proxy = get_account( a.proxy );

      modify( proxy, [&]( account_object& a )
      {
         for( int i = GAMEBANK_MAX_PROXY_RECURSION_DEPTH - depth - 1; i >= 0; --i )
         {
            a.proxied_vsf_votes[i+depth] += delta[i];
         }
      } );

      adjust_proxied_witness_votes( proxy, delta, depth + 1 );
   }
   else
   {
      share_type total_delta = 0;
      for( int i = GAMEBANK_MAX_PROXY_RECURSION_DEPTH - depth; i >= 0; --i )
         total_delta += delta[i];
      adjust_witness_votes( a, total_delta );
   }
}

void database::adjust_proxied_witness_votes( const account_object& a, share_type delta, int depth )
{
   if( a.proxy != GAMEBANK_PROXY_TO_SELF_ACCOUNT )
   {
      /// nested proxies are not supported, vote will not propagate
      if( depth >= GAMEBANK_MAX_PROXY_RECURSION_DEPTH )
         return;

      const auto& proxy = get_account( a.proxy );

      modify( proxy, [&]( account_object& a )
      {
         a.proxied_vsf_votes[depth] += delta;
      } );

      adjust_proxied_witness_votes( proxy, delta, depth + 1 );
   }
   else
   {
     adjust_witness_votes( a, delta );
   }
}

void database::adjust_witness_votes( const account_object& a, share_type delta )
{
   const auto& vidx = get_index< witness_vote_index >().indices().get< by_account_witness >();
   auto itr = vidx.lower_bound( boost::make_tuple( a.name, account_name_type() ) );
   while( itr != vidx.end() && itr->account == a.name )
   {
      adjust_witness_vote( get< witness_object, by_name >(itr->witness), delta );
      ++itr;
   }
}

void database::adjust_witness_vote( const witness_object& witness, share_type delta )
{
   const witness_schedule_object& wso = get_witness_schedule_object();
   modify( witness, [&]( witness_object& w )
   {
      auto delta_pos = w.votes.value * (wso.current_virtual_time - w.virtual_last_update);
      w.virtual_position += delta_pos;

      w.virtual_last_update = wso.current_virtual_time;
      w.votes += delta;
      FC_ASSERT( w.votes <= get_dynamic_global_properties().total_vesting_shares.amount, "", ("w.votes", w.votes)("props",get_dynamic_global_properties().total_vesting_shares) );

      w.virtual_scheduled_time = w.virtual_last_update + (GAMEBANK_VIRTUAL_SCHEDULE_LAP_LENGTH2 - w.virtual_position)/(w.votes.value+1);
 
      /** witnesses with a low number of votes could overflow the time field and end up with a scheduled time in the past */

         if( w.virtual_scheduled_time < wso.current_virtual_time )
            w.virtual_scheduled_time = fc::uint128::max_value();

   } );
}

void database::clear_witness_votes( const account_object& a )
{
   const auto& vidx = get_index< witness_vote_index >().indices().get<by_account_witness>();
   auto itr = vidx.lower_bound( boost::make_tuple( a.name, account_name_type() ) );
   while( itr != vidx.end() && itr->account == a.name )
   {
      const auto& current = *itr;
      ++itr;
      remove(current);
   }

      modify( a, [&](account_object& acc )
      {
         acc.witnesses_voted_for = 0;
      });
}

void database::clear_null_account_balance()
{
   const auto& null_account = get_account( GAMEBANK_NULL_ACCOUNT );
   asset total_gbc( 0, GBC_SYMBOL );
   asset total_gbd( 0, GBD_SYMBOL );

   if( null_account.balance.amount > 0 )
   {
      total_gbc += null_account.balance;
      adjust_balance( null_account, -null_account.balance );
   }

   if( null_account.savings_balance.amount > 0 )
   {
      total_gbc += null_account.savings_balance;
      adjust_savings_balance( null_account, -null_account.savings_balance );
   }

   if( null_account.gbd_balance.amount > 0 )
   {
      total_gbd += null_account.gbd_balance;
      adjust_balance( null_account, -null_account.gbd_balance );
   }

   if( null_account.savings_gbd_balance.amount > 0 )
   {
      total_gbd += null_account.savings_gbd_balance;
      adjust_savings_balance( null_account, -null_account.savings_gbd_balance );
   }

   if( null_account.vesting_shares.amount > 0 )
   {
      const auto& gpo = get_dynamic_global_properties();
      auto converted_gbc = null_account.vesting_shares * gpo.get_vesting_share_price();

      modify( gpo, [&]( dynamic_global_property_object& g )
      {
         g.total_vesting_shares -= null_account.vesting_shares;
         g.total_vesting_fund_gbc -= converted_gbc;
      });

      modify( null_account, [&]( account_object& a )
      {
         a.vesting_shares.amount = 0;
      });

      total_gbc += converted_gbc;
   }

   if( null_account.reward_gbc_balance.amount > 0 )
   {
      total_gbc += null_account.reward_gbc_balance;
      adjust_reward_balance( null_account, -null_account.reward_gbc_balance );
   }

   if( null_account.reward_gbd_balance.amount > 0 )
   {
      total_gbd += null_account.reward_gbd_balance;
      adjust_reward_balance( null_account, -null_account.reward_gbd_balance );
   }

   if( null_account.reward_vesting_balance.amount > 0 )
   {
      const auto& gpo = get_dynamic_global_properties();

      total_gbc += null_account.reward_vesting_gbc;

      modify( gpo, [&]( dynamic_global_property_object& g )
      {
         g.pending_rewarded_vesting_shares -= null_account.reward_vesting_balance;
         g.pending_rewarded_vesting_gbc -= null_account.reward_vesting_gbc;
      });

      modify( null_account, [&]( account_object& a )
      {
         a.reward_vesting_gbc.amount = 0;
         a.reward_vesting_balance.amount = 0;
      });
   }

   if( total_gbc.amount > 0 )
      adjust_supply( -total_gbc );

   if( total_gbd.amount > 0 )
      adjust_supply( -total_gbd );
}

/**
 * This method updates total_reward_shares2 on DGPO, and children_rshares2 on comments, when a comment's rshares2 changes
 * from old_rshares2 to new_rshares2.  Maintaining invariants that children_rshares2 is the sum of all descendants' rshares2,
 * and dgpo.total_reward_shares2 is the total number of rshares2 outstanding.
 */
void database::adjust_rshares2( const comment_object& c, fc::uint128_t old_rshares2, fc::uint128_t new_rshares2 )
{

   const auto& dgpo = get_dynamic_global_properties();
   modify( dgpo, [&]( dynamic_global_property_object& p )
   {
      p.total_reward_shares2 -= old_rshares2;
      p.total_reward_shares2 += new_rshares2;
   } );
}

void database::update_owner_authority( const account_object& account, const authority& owner_authority )
{
   if( head_block_num() >= GAMEBANK_OWNER_AUTH_HISTORY_TRACKING_START_BLOCK_NUM )
   {
      //记录账号的owner权限更新记录
      create< owner_authority_history_object >( [&]( owner_authority_history_object& hist )
      {
         hist.account = account.name;
		 //保存旧的owner权限
         hist.previous_owner_authority = get< account_authority_object, by_account >( account.name ).owner;
         hist.last_valid_time = head_block_time();
      });
   }
  //更新为新的owner权限
   modify( get< account_authority_object, by_account >( account.name ), [&]( account_authority_object& auth )
   {
      auth.owner = owner_authority;
      auth.last_owner_update = head_block_time();
   });
}

//@called in _apply_block
void database::process_vesting_withdrawals()
{
   const auto& widx = get_index< account_index, by_next_vesting_withdrawal >();
   const auto& didx = get_index< withdraw_vesting_route_index, by_withdraw_route >();
   auto current = widx.begin();

   const auto& cprops = get_dynamic_global_properties();

   while( current != widx.end() && current->next_vesting_withdrawal <= head_block_time() )
   {
      const auto& from_account = *current; ++current;

      /**
      *  Let T = total tokens in vesting fund
      *  Let V = total vesting shares
      *  Let v = total vesting shares being cashed out
      *  用户可套现代币数?
      *  The user may withdraw  vT / V tokens
      */
      share_type to_withdraw;
      if ( from_account.to_withdraw - from_account.withdrawn < from_account.vesting_withdraw_rate.amount )
         to_withdraw = std::min( from_account.vesting_shares.amount, from_account.to_withdraw % from_account.vesting_withdraw_rate.amount ).value;
      else
         to_withdraw = std::min( from_account.vesting_shares.amount, from_account.vesting_withdraw_rate.amount ).value;

      share_type vests_deposited_as_gbc = 0;
      share_type vests_deposited_as_vests = 0;
      asset total_gbc_converted = asset( 0, GBC_SYMBOL );

      // Do two passes, the first for vests, the second for gbc. Try to maintain as much accuracy for vests as possible.
      for( auto itr = didx.upper_bound( boost::make_tuple( from_account.name, account_name_type() ) );
           itr != didx.end() && itr->from_account == from_account.name;
           ++itr )
      {
         if( itr->auto_vest )
         {
            share_type to_deposit = ( ( fc::uint128_t ( to_withdraw.value ) * itr->percent ) / GAMEBANK_100_PERCENT ).to_uint64();
            vests_deposited_as_vests += to_deposit;

            if( to_deposit > 0 )
            {
               const auto& to_account = get< account_object, by_name >( itr->to_account );

               modify( to_account, [&]( account_object& a )
               {
                  a.vesting_shares.amount += to_deposit;
               });

               adjust_proxied_witness_votes( to_account, to_deposit );

               push_virtual_operation( fill_vesting_withdraw_operation( from_account.name, to_account.name, asset( to_deposit, GBS_SYMBOL ), asset( to_deposit, GBS_SYMBOL ) ) );
            }
         }
      }

      for( auto itr = didx.upper_bound( boost::make_tuple( from_account.name, account_name_type() ) );
           itr != didx.end() && itr->from_account == from_account.name;
           ++itr )
      {
         if( !itr->auto_vest )
         {
            const auto& to_account = get< account_object, by_name >( itr->to_account );

            share_type to_deposit = ( ( fc::uint128_t ( to_withdraw.value ) * itr->percent ) / GAMEBANK_100_PERCENT ).to_uint64();
            vests_deposited_as_gbc += to_deposit;
            auto converted_gbc = asset( to_deposit, GBS_SYMBOL ) * cprops.get_vesting_share_price();
            total_gbc_converted += converted_gbc;

            if( to_deposit > 0 )
            {
               modify( to_account, [&]( account_object& a )
               {
                  a.balance += converted_gbc;
               });

               modify( cprops, [&]( dynamic_global_property_object& o )
               {
                  o.total_vesting_fund_gbc -= converted_gbc;
                  o.total_vesting_shares.amount -= to_deposit;
               });

               push_virtual_operation( fill_vesting_withdraw_operation( from_account.name, to_account.name, asset( to_deposit, GBS_SYMBOL), converted_gbc ) );
            }
         }
      }

      share_type to_convert = to_withdraw - vests_deposited_as_gbc - vests_deposited_as_vests;
      FC_ASSERT( to_convert >= 0, "Deposited more vests than were supposed to be withdrawn" );

      auto converted_gbc = asset( to_convert, GBS_SYMBOL ) * cprops.get_vesting_share_price();

      modify( from_account, [&]( account_object& a )
      {
         a.vesting_shares.amount -= to_withdraw;
         a.balance += converted_gbc;
         a.withdrawn += to_withdraw;

         if( a.withdrawn >= a.to_withdraw || a.vesting_shares.amount == 0 )
         {
            a.vesting_withdraw_rate.amount = 0;
            a.next_vesting_withdrawal = fc::time_point_sec::maximum();
         }
         else
         {
            a.next_vesting_withdrawal += fc::seconds( GAMEBANK_VESTING_WITHDRAW_INTERVAL_SECONDS );
         }
      });

      modify( cprops, [&]( dynamic_global_property_object& o )
      {
         o.total_vesting_fund_gbc -= converted_gbc;
         o.total_vesting_shares.amount -= to_convert;
      });

      if( to_withdraw > 0 )
         adjust_proxied_witness_votes( from_account, -to_withdraw );

      push_virtual_operation( fill_vesting_withdraw_operation( from_account.name, from_account.name, asset( to_convert, GBS_SYMBOL ), converted_gbc ) );
   }
}

void database::adjust_total_payout( const comment_object& cur, const asset& gbd_created, const asset& curator_gbd_value, const asset& beneficiary_value )
{
   modify( cur, [&]( comment_object& c )
   {
      // input assets should be in gbd
      c.total_payout_value += gbd_created;
      c.curator_payout_value += curator_gbd_value;
      c.beneficiary_payout_value += beneficiary_value;
   } );
   /// TODO: potentially modify author's total payout numbers as well
}

/**
 *  This method will iterate through all comment_vote_objects and give them
 *  (max_rewards * weight) / c.total_vote_weight.
 *
 *  @returns unclaimed rewards.
 */
 //curators奖励分配原则：
 //1 每条comment有多个投票者投票，每个投票的权重决定投票者应得收益
 //2 分配给curators(投票的账户)的奖励最终转换为GBC POWER

//max_rewards: 支付给curators的最大奖励
 //返回值：未被认领的奖励
share_type database::pay_curators( const comment_object& c, share_type& max_rewards )
{
   struct cmp
   {
      bool operator()( const comment_vote_object* obj, const comment_vote_object* obj2 ) const
      {
      //权重相同，比较voter
      //权重不同，比较权重
         if( obj->weight == obj2->weight )
            return obj->voter < obj2->voter;
         else
            return obj->weight > obj2->weight;
      }
   };

   try
   {
      //本条评论的所有投票权重
      uint128_t total_weight( c.total_vote_weight );
      //edump( (total_weight)(max_rewards) );
      share_type unclaimed_rewards = max_rewards;

      if( !c.allow_curation_rewards )
      {
         unclaimed_rewards = 0;
         max_rewards = 0;
      }
      else if( c.total_vote_weight > 0 )
      {
         const auto& cvidx = get_index<comment_vote_index>().indices().get<by_comment_voter>();
         auto itr = cvidx.lower_bound( c.id );

         std::set< const comment_vote_object*, cmp > proxy_set;//有序
         while( itr != cvidx.end() && itr->comment == c.id )
         {
            proxy_set.insert( &( *itr ) );
            ++itr;
         }

         for( auto& item : proxy_set )
         {
            //一条vote的权重
            uint128_t weight( item->weight );
			//以vote的权重为依据，计算这条vote所获收益
            auto claim = ( ( max_rewards.value * weight ) / total_weight ).to_uint64();
            if( claim > 0 ) // min_amt is non-zero satoshis
            {
               unclaimed_rewards -= claim;
               const auto& voter = get( item->voter );
			   //投票收益转为SP并存入投票人账户
               auto reward = create_vesting( voter, asset( claim, GBC_SYMBOL ), true );

			   //操作curation奖励
               push_virtual_operation( curation_reward_operation( voter.name, reward, c.author, to_string( c.permlink ) ) );

               #ifndef IS_LOW_MEM
                  modify( voter, [&]( account_object& a )
                  {
                     a.curation_rewards += claim;
                  });
               #endif
            }
         }
      }
      max_rewards -= unclaimed_rewards;

      return unclaimed_rewards;
   } FC_CAPTURE_AND_RETHROW()
}

void fill_comment_reward_context_local_state( util::comment_reward_context& ctx, const comment_object& comment )
{
   ctx.rshares = comment.net_rshares;			//帖子因投票产生的rshares
   ctx.reward_weight = comment.reward_weight;	
   ctx.max_gbd = comment.max_accepted_payout;
}

share_type database::cashout_comment_helper( util::comment_reward_context& ctx, const comment_object& comment, bool forward_curation_remainder )
{
   try
   {
      share_type claimed_reward = 0;

      if( comment.net_rshares > 0 )
      {
         //填充comment_reward_context
         fill_comment_reward_context_local_state( ctx, comment );

         const auto rf = get_reward_fund( comment );
         ctx.reward_curve = rf.author_reward_curve;
         ctx.content_constant = rf.content_constant;

         const share_type reward = util::get_rshare_reward( ctx );
         uint128_t reward_tokens = uint128_t( reward.value );

         if( reward_tokens > 0 )
         {
			//给curation的收益分配
            share_type curation_tokens = ( ( reward_tokens * get_curation_rewards_percent( comment ) ) / GAMEBANK_100_PERCENT ).to_uint64();
            share_type author_tokens = reward_tokens.to_uint64() - curation_tokens;

			//curation获得SP，pay_curators返回未被认领的收益
            share_type curation_remainder = pay_curators( comment, curation_tokens );

            if( forward_curation_remainder )
               author_tokens += curation_remainder;

            share_type total_beneficiary = 0;
			//post作者和投票者奖励和
            claimed_reward = author_tokens + curation_tokens;

            for( auto& b : comment.beneficiaries )
            {
               auto benefactor_tokens = ( author_tokens * b.weight ) / GAMEBANK_100_PERCENT;
               auto vest_created = create_vesting( get_account( b.account ), asset( benefactor_tokens, GBC_SYMBOL ), true );
               push_virtual_operation( comment_benefactor_reward_operation( b.account, comment.author, to_string( comment.permlink ), vest_created ) );
               total_beneficiary += benefactor_tokens;
            }
            //确定author的分配
            author_tokens -= total_beneficiary;
			
            //auto gbd_gbc     = ( author_tokens * comment.percent_gamebank_dollars ) / ( 2 * GAMEBANK_100_PERCENT ) ;
			auto gbc_to_author = (author_tokens * GAMEBANK_100_PERCENT) / (2 * GAMEBANK_100_PERCENT);
            auto vesting_gbc = author_tokens - gbc_to_author;

            const auto& author = get_account( comment.author );
			//vest to author
            auto vest_created = create_vesting( author, asset( vesting_gbc, GBC_SYMBOL ), true );
			//以GBD/GBC分配给author
            //auto gbd_payout = create_gbd( author, asset( gbd_gbc, GBC_SYMBOL ), true );
			//not use gbd to author
			adjust_balance(author, asset( gbc_to_author, GBC_SYMBOL));

            //adjust_total_payout( comment, gbd_payout.first + to_gbd( gbd_payout.second + asset( vesting_gbc, GBC_SYMBOL ) ), to_gbd( asset( curation_tokens, GBC_SYMBOL ) ), to_gbd( asset( total_beneficiary, GBC_SYMBOL ) ) );
            //以GBC单位记录评论收益
			adjust_total_payout( comment, asset(gbc_to_author, GBC_SYMBOL), asset( curation_tokens, GBC_SYMBOL ), asset( total_beneficiary, GBC_SYMBOL ));

			//作者奖励操作：需要GBD奖励值+GBC奖励值+GBS奖励值
            //push_virtual_operation( author_reward_operation( comment.author, to_string( comment.permlink ), gbd_payout.first, gbd_payout.second, vest_created ) );
            push_virtual_operation( author_reward_operation( comment.author, to_string( comment.permlink ), asset(0, GBD_SYMBOL), asset(gbc_to_author, GBC_SYMBOL), vest_created ) );
			
			//作者+投票者总奖励
            //push_virtual_operation( comment_reward_operation( comment.author, to_string( comment.permlink ), to_gbd( asset( claimed_reward, GBC_SYMBOL ) ) ) );
            push_virtual_operation( comment_reward_operation( comment.author, to_string( comment.permlink ), asset( claimed_reward, GBC_SYMBOL ) )  );

			//投票者奖励见curation_reward_operation
            #ifndef IS_LOW_MEM
               modify( comment, [&]( comment_object& c )
               {
                  c.author_rewards += author_tokens;
               });

               modify( get_account( comment.author ), [&]( account_object& a )
               {
                  a.posting_rewards += author_tokens;
               });
            #endif

         }

      }

      modify( comment, [&]( comment_object& c )
      {
         /**
         * A payout is only made for positive rshares, negative rshares hang around
         * for the next time this post might get an upvote.
         */
         if( c.net_rshares > 0 )
            c.net_rshares = 0;
         c.children_abs_rshares = 0;
         c.abs_rshares  = 0;
         c.vote_rshares = 0;
         c.total_vote_weight = 0;
         c.max_cashout_time = fc::time_point_sec::maximum();

		//@see calculate_discussion_payout_time
         c.cashout_time = fc::time_point_sec::maximum();


         c.last_payout = head_block_time();
      } );

      push_virtual_operation( comment_payout_update_operation( comment.author, to_string( comment.permlink ) ) );

      const auto& vote_idx = get_index< comment_vote_index >().indices().get< by_comment_voter >();
      auto vote_itr = vote_idx.lower_bound( comment.id );
	  //遍历所有投票个这条评论(comment_object)的投票对象(comment_vote_index)
      while( vote_itr != vote_idx.end() && vote_itr->comment == comment.id )
      {
         const auto& cur_vote = *vote_itr;
         ++vote_itr;
         if( calculate_discussion_payout_time( comment ) != fc::time_point_sec::maximum() )
         {
            modify( cur_vote, [&]( comment_vote_object& cvo )
            {
               cvo.num_changes = -1;
            });
         }
         else
         {
#ifdef CLEAR_VOTES
            remove( cur_vote );
#endif
         }
      }

      return claimed_reward;
   } FC_CAPTURE_AND_RETHROW( (comment) )
}

void database::process_comment_cashout()
{
   /// don't allow any content to get paid out until the website is ready to launch
   /// and people have had a week to start posting.  The first cashout will be the biggest because it
   /// will represent 2+ months of rewards.


   //const auto& gpo = get_dynamic_global_properties();
   util::comment_reward_context ctx;
   ctx.current_gbc_price = get_feed_history().current_median_history;

   vector< reward_fund_context > funds;
   vector< share_type > gbc_awarded;
   const auto& reward_idx = get_index< reward_fund_index, by_id >();	//contain reward_fund_object

   // Decay recent rshares of each fund
   for( auto itr = reward_idx.begin(); itr != reward_idx.end(); ++itr )
   {
      // Add all reward funds to the local cache and decay their recent rshares
      modify( *itr, [&]( reward_fund_object& rfo )
      {
         fc::microseconds decay_time;

         decay_time = GAMEBANK_RECENT_RSHARES_DECAY_TIME;//15天

         //decay their recent rshares
		 //随时间衰减recent_claims，时间越长，衰减越多
		 //为何要衰减?
         rfo.recent_claims -= ( rfo.recent_claims * ( head_block_time() - rfo.last_update ).to_seconds() ) / decay_time.to_seconds();
         rfo.last_update = head_block_time();
      });

      reward_fund_context rf_ctx;
      rf_ctx.recent_claims = itr->recent_claims;
      rf_ctx.reward_balance = itr->reward_balance;

      // The index is by ID, so the ID should be the current size of the vector (0, 1, 2, etc...)
      //id即vector<reward_fund_context>下标
      assert( funds.size() == static_cast<size_t>(itr->id._id) );
      //to the local cache
      funds.push_back( rf_ctx );
   }

   const auto& cidx        = get_index< comment_index >().indices().get< by_cashout_time >();
   //const auto& com_by_root = get_index< comment_index >().indices().get< by_root >();

   auto current = cidx.begin();
   //  add all rshares about to be cashed out to the reward funds. This ensures equal satoshi per rshare payment
		//遍历所有即将cashout的帖子，将每个帖子的总奖励加入奖励funds
      while( current != cidx.end() && current->cashout_time <= head_block_time() )
      {
         if( current->net_rshares > 0 )//帖子因投票(包括upVote和downVote)产生的rshares
         {
            const auto& rf = get_reward_fund( *current );//return reward_fund_object
            //计算每个帖子的总奖励值
            //evaluate_reward_curve： 运用奖励计算方式curve，对帖子因投票产生的rshares做奖励计算.返回rshares对应的奖励值
            funds[ rf.id._id ].recent_claims += util::evaluate_reward_curve( current->net_rshares.value, rf.author_reward_curve, rf.content_constant );
         }

         ++current;
      }

      current = cidx.begin();

   /*
    * Payout all comments
    *
    * Each payout follows a similar pattern, but for a different reason.
    * Cashout comment helper does not know about the reward fund it is paying from.
    * The helper only does token allocation based on curation rewards and the GBD
    * global %, etc.
    *
    * Each context is used by get_rshare_reward to determine what part of each budget
    * the comment is entitled to. Prior to hardfork 17, all payouts are done against
    * the global state updated each payout. After the hardfork, each payout is done
    * against a reward fund state that is snapshotted before all payouts in the block.
    */
   while( current != cidx.end() && current->cashout_time <= head_block_time() )
   {
         auto fund_id = get_reward_fund( *current ).id._id;
         ctx.total_reward_shares2 = funds[ fund_id ].recent_claims;
         ctx.total_reward_fund_gbc = funds[ fund_id ].reward_balance;

         bool forward_curation_remainder = !has_hardfork( GAMEBANK_HARDFORK_0_1 );
         funds[ fund_id ].gbc_awarded += cashout_comment_helper( ctx, *current, forward_curation_remainder );
      

      current = cidx.begin();
   }

   // Write the cached fund state back to the database
   //fund的变化重新写入db
   if( funds.size() )
   {
      for( size_t i = 0; i < funds.size(); i++ )
      {
         modify( get< reward_fund_object, by_id >( reward_fund_id_type( i ) ), [&]( reward_fund_object& rfo )
         {
            rfo.recent_claims = funds[ i ].recent_claims;
		//支付收益后，从本贴总奖池减去支付的奖励
            rfo.reward_balance -= asset( funds[ i ].gbc_awarded, GBC_SYMBOL );//本贴奖池
         });
      }
   }
}

/**网络每年通货膨胀率102%（如何理解virtual）
 *  Overall the network has an inflation rate of 102% of virtual gbc per year
 *  90% of inflation is directed to vesting shares
 *  10% of inflation is directed to subjective proof of work voting
 *  1% of inflation is directed to liquidity providers
 *  1% of inflation is directed to block producers
 *
 *  This method pays out vesting and reward shares every block, and liquidity shares once per day.
 *  This method does not pay out witnesses.
 */
 //called in _apply_block
void database::process_funds()
{
   const auto& props = get_dynamic_global_properties();
   const auto& wso = get_witness_schedule_object();
      /**
       * At block 7,000,000 have a 9.5% instantaneous inflation rate, decreasing to 0.95% at a rate of 0.01%
       * every 250k blocks. This narrowing will take approximately 20.5 years and will complete on block 220,750,000
       */
      int64_t start_inflation_rate = int64_t( GAMEBANK_INFLATION_RATE_START_PERCENT );
      int64_t inflation_rate_adjustment = int64_t( head_block_num() / GAMEBANK_INFLATION_NARROWING_PERIOD );
      int64_t inflation_rate_floor = int64_t( GAMEBANK_INFLATION_RATE_STOP_PERCENT );

      // below subtraction cannot underflow int64_t because inflation_rate_adjustment is <2^32
      int64_t current_inflation_rate = std::max( start_inflation_rate - inflation_rate_adjustment, inflation_rate_floor );
/*
Current Allocation & Supply
Of the new tokens that are generated, 75% go to fund the reward pool, which is split between authors and
curators. Another 15% of the new tokens are awarded to holders of SP. The remaining 10% pays for the
witnesses to power the blockchain.
*/
	  //每生成一个新区块而新增的代币！
	  //props.virtual_supply.amount * current_inflation_rate 代表当前年通胀率下货币每年新增量
      auto new_gbc = ( props.virtual_supply.amount * current_inflation_rate ) / ( int64_t( GAMEBANK_100_PERCENT ) * int64_t( GAMEBANK_BLOCKS_PER_YEAR ) );
	  
	  //新增代币的75% to reward pool
      auto content_reward = ( new_gbc * GAMEBANK_CONTENT_REWARD_PERCENT ) / GAMEBANK_100_PERCENT;
      
      content_reward = pay_reward_funds( content_reward ); /// 75% to content creator
	  
	  //新增代币的15% to vesting fund，授予给SP持有者
      auto vesting_reward = ( new_gbc * GAMEBANK_VESTING_FUND_PERCENT ) / GAMEBANK_100_PERCENT; /// 15% to vesting fund
	  
	  //新增代币的 10% to witness pay（power the blockchain）
      auto witness_reward = new_gbc - content_reward - vesting_reward; /// Remaining 10% to witness pay

      const auto& cwit = get_witness( props.current_witness );

      witness_reward *= GAMEBANK_MAX_WITNESSES;

	//三种不同类型的见证人，获取的witness_reward因各自权重的不同而不同
      if( cwit.schedule == witness_object::timeshare )
         witness_reward *= wso.timeshare_weight;
      else if( cwit.schedule == witness_object::miner )
         witness_reward *= wso.miner_weight;
      else if( cwit.schedule == witness_object::top19 )
         witness_reward *= wso.top19_weight;
      else
         wlog( "Encountered unknown witness type for witness: ${w}", ("w", cwit.owner) );
     
      witness_reward /= wso.witness_pay_normalization_factor;
      new_gbc = content_reward + vesting_reward + witness_reward;

      modify( props, [&]( dynamic_global_property_object& p )
      {
		//SP持有者总奖池
         p.total_vesting_fund_gbc += asset( vesting_reward, GBC_SYMBOL );
         p.current_supply           += asset( new_gbc, GBC_SYMBOL );
         p.virtual_supply           += asset( new_gbc, GBC_SYMBOL );
      });

		//生成区块的见证人奖励
      const auto& producer_reward = create_vesting( get_account( cwit.owner ), asset( witness_reward, GBC_SYMBOL ) );
      push_virtual_operation( producer_reward_operation( cwit.owner, producer_reward ) );
   
}

void database::process_savings_withdraws()
{
  const auto& idx = get_index< savings_withdraw_index >().indices().get< by_complete_from_rid >();
  auto itr = idx.begin();
  while( itr != idx.end() ) {
     if( itr->complete > head_block_time() )
        break;
     adjust_balance( get_account( itr->to ), itr->amount );

     modify( get_account( itr->from ), [&]( account_object& a )
     {
        a.savings_withdraw_requests--;
     });

     push_virtual_operation( fill_transfer_from_savings_operation( itr->from, itr->to, itr->amount, itr->request_id, to_string( itr->memo) ) );

     remove( *itr );
     itr = idx.begin();
  }
}

void database::process_crowdfunding()
{
    const auto& idx = get_index< crowdfunding_index >().indices().get< by_expire_originator >();
    auto itr = idx.begin();
    while ( itr != idx.end() ) 
    {
        if ( itr->expire > head_block_time() )
            break;
        if ( itr->finish > 0 )
            break;
        int32_t finish = 1;
        if (itr->total_raise_value > itr->curator_raise_value)
        {
            finish = 2;
            const auto& invest_idx = get_index< crowdfunding_invest_index >().indices().get< by_expire_invester >();
            auto invest_itr = invest_idx.begin();
            while ( invest_itr != invest_idx.end() ) 
            {
                if (invest_itr->expire > head_block_time() )
                    break;
                adjust_balance(get_account(invest_itr->invester), invest_itr->raise);

                remove(*invest_itr);
                invest_itr = invest_idx.begin();
            }
         }
        else
        {
            adjust_balance(get_account(itr->originator), itr->curator_raise_value);
        }

        modify(*itr, [&](crowdfunding_object& co)
        {
            co.finish = finish;
        });

        //modify(get_account(itr->originator), [&](account_object& a)
        //{
        //    a.crowdfunding_count--;
        //});

        //remove( *itr );
        itr = idx.begin();
    }
}

//流动性奖励
/*
解析：流动性奖励指的是奖励给做市商的奖励。

   GBD和gamebank的交易市场会有做市商，一个小时内分数最高的做市商能获得1200个gbc。然后分数清0.

分数计算公式：

提现点数=净报价量（买价量）*净询价量（卖价量）

          Bid_volume*ask/offer_volume

         相当于是做成的买入的单*卖出的单。

*/
//每区块1gbc（3秒一个区块，每小时1200gbc）或者每年增发总量的0.750%，以较高者为准
asset database::get_liquidity_reward()const
{
   return asset( 0, GBC_SYMBOL );

   //const auto& props = get_dynamic_global_properties();
   //static_assert( GAMEBANK_LIQUIDITY_REWARD_PERIOD_SEC == 60*60, "this code assumes a 1 hour time interval" );
   //asset percent( protocol::calc_percent_reward_per_hour< GAMEBANK_LIQUIDITY_APR_PERCENT >( props.virtual_supply.amount ), GBC_SYMBOL );
   //return std::max( percent, GAMEBANK_MIN_LIQUIDITY_REWARD );
}
//内容创造奖励(发帖)
//每区块1gbc或者每年增发总量的3.875%，以较高者为准
asset database::get_content_reward()const
{
   const auto& props = get_dynamic_global_properties();
   static_assert( GAMEBANK_BLOCK_INTERVAL == 3, "this code assumes a 3-second time interval" );
   asset percent( protocol::calc_percent_reward_per_block< GAMEBANK_CONTENT_APR_PERCENT >( props.virtual_supply.amount ), GBC_SYMBOL );
   return std::max( percent, GAMEBANK_MIN_CONTENT_REWARD );
}
//策展奖励(点赞，评论)
asset database::get_curation_reward()const
{
   const auto& props = get_dynamic_global_properties();
   static_assert( GAMEBANK_BLOCK_INTERVAL == 3, "this code assumes a 3-second time interval" );
   asset percent( protocol::calc_percent_reward_per_block< GAMEBANK_CURATE_APR_PERCENT >( props.virtual_supply.amount ), GBC_SYMBOL);
   return std::max( percent, GAMEBANK_MIN_CURATE_REWARD );
}

//区块生产奖励：每区块1gbc或者每年增发总量的0.750%
asset database::get_producer_reward()
{
   const auto& props = get_dynamic_global_properties();
   static_assert( GAMEBANK_BLOCK_INTERVAL == 3, "this code assumes a 3-second time interval" );
   asset percent( protocol::calc_percent_reward_per_block< GAMEBANK_PRODUCER_APR_PERCENT >( props.virtual_supply.amount ), GBC_SYMBOL);
   auto pay = std::max( percent, GAMEBANK_MIN_PRODUCER_REWARD );
   const auto& witness_account = get_account( props.current_witness );

   /// pay witness in vesting shares
   if( props.head_block_number >= GAMEBANK_START_MINER_VOTING_BLOCK || (witness_account.vesting_shares.amount.value == 0) ) {
      // const auto& witness_obj = get_witness( props.current_witness );
      const auto& producer_reward = create_vesting( witness_account, pay );
      push_virtual_operation( producer_reward_operation( witness_account.name, producer_reward ) );
   }
   else
   {
      modify( get_account( witness_account.name), [&]( account_object& a )
      {
         a.balance += pay;
      } );
   }

   return pay;
}

asset database::get_pow_reward()const
{
   const auto& props = get_dynamic_global_properties();

#ifndef IS_TEST_NET
   /// 0 block rewards until at least GAMEBANK_MAX_WITNESSES have produced a POW
   if( props.num_pow_witnesses < GAMEBANK_MAX_WITNESSES && props.head_block_number < GAMEBANK_START_VESTING_BLOCK )
      return asset( 0, GBC_SYMBOL );
#endif

   static_assert( GAMEBANK_BLOCK_INTERVAL == 3, "this code assumes a 3-second time interval" );
   static_assert( GAMEBANK_MAX_WITNESSES == 21, "this code assumes 21 per round" );
   asset percent( calc_percent_reward_per_round< GAMEBANK_POW_APR_PERCENT >( props.virtual_supply.amount ), GBC_SYMBOL);
   return std::max( percent, GAMEBANK_MIN_POW_REWARD );//GAMEBANK_MIN_POW_REWARD： asset( 1000, GBC_SYMBOL )
}


void database::pay_liquidity_reward()
{
#ifdef IS_TEST_NET
   if( !liquidity_rewards_enabled )
      return;
#endif

   if( (head_block_num() % GAMEBANK_LIQUIDITY_REWARD_BLOCKS) == 0 )
   {
      auto reward = get_liquidity_reward();

      if( reward.amount == 0 )
         return;

      const auto& ridx = get_index< liquidity_reward_balance_index >().indices().get< by_volume_weight >();
      auto itr = ridx.begin();
      if( itr != ridx.end() && itr->volume_weight() > 0 )
      {
         adjust_supply( reward, true );
         adjust_balance( get(itr->owner), reward );
         modify( *itr, [&]( liquidity_reward_balance_object& obj )
         {
            obj.gbc_volume = 0;
            obj.gbd_volume   = 0;
            obj.last_update  = head_block_time();
            obj.weight = 0;
         } );

         push_virtual_operation( liquidity_reward_operation( get(itr->owner).name, reward ) );
      }
   }
}

uint16_t database::get_curation_rewards_percent( const comment_object& c ) const
{
   return get_reward_fund( c ).percent_curation_rewards;
}

//HF17
share_type database::pay_reward_funds( share_type reward )
{
   const auto& reward_idx = get_index< reward_fund_index, by_id >();
   share_type used_rewards = 0;

   for( auto itr = reward_idx.begin(); itr != reward_idx.end(); ++itr )
   {
      // reward is a per block reward and the percents are 16-bit. This should never overflow
      auto r = ( reward * itr->percent_content_rewards ) / GAMEBANK_100_PERCENT;

      modify( *itr, [&]( reward_fund_object& rfo )
      {
         rfo.reward_balance += asset( r, GBC_SYMBOL );
      });

      used_rewards += r;

      // Sanity check to ensure we aren't printing more GBC than has been allocated through inflation
      FC_ASSERT( used_rewards <= reward );
   }

   return used_rewards;
}

/**
 *  Iterates over all conversion requests with a conversion date before
 *  the head block time and then converts them to/from gbc/gbd at the
 *  current median price feed history price times the premium
 */
 //处理所有的转换请求（GBD->GBC...）
 //在_apply_block中调用
void database::process_conversions()
{
   auto now = head_block_time();
   const auto& request_by_date = get_index< convert_request_index >().indices().get< by_conversion_date >();
   auto itr = request_by_date.begin();

   const auto& fhistory = get_feed_history();
   if( fhistory.current_median_history.is_null() )
      return;

   asset net_gbd( 0, GBD_SYMBOL );	//记录本次处理所有请求后的被兑换的GBD总量
   asset net_gbc( 0, GBC_SYMBOL );	//记录本次处理所有请求后的兑换得到的GBC总量

   //遍历所有转换请求（这些请求必须达到预设的处理时间才可以被处理）
   while( itr != request_by_date.end() && itr->conversion_date <= now )
   {
      //按照喂价计算兑换后的GBC
      //current_median_history在请求发起后的7天/3.5天时间里更新
      auto amount_to_issue = itr->amount * fhistory.current_median_history;

	  //增加账户GBC存款(账户的GBD存款已在发起兑换请求时已被减去)
      adjust_balance( itr->owner, amount_to_issue );

      net_gbd   += itr->amount;//被兑换的gbd
      net_gbc += amount_to_issue;//要新增的gbc

      push_virtual_operation( fill_convert_request_operation ( itr->owner, itr->requestid, itr->amount, amount_to_issue ) );
      //处理完毕，从DB移除请求
      remove( *itr );
      itr = request_by_date.begin();
   }

   //将兑换的GBD/GBC总量记录到全局属性，用作?
   const auto& props = get_dynamic_global_properties();
   modify( props, [&]( dynamic_global_property_object& p )
   {
       p.current_supply += net_gbc;		//更新当前GBC总量
       p.current_gbd_supply -= net_gbd;	//更新当前GBD总量
		//系统价值的gamebank总量
       p.virtual_supply += net_gbc;
       p.virtual_supply -= net_gbd * get_feed_history().current_median_history;
   } );
}

asset database::to_gbd( const asset& gbc )const
{
   return util::to_gbd( get_feed_history().current_median_history, gbc );
}

asset database::to_gbc( const asset& gbd )const
{
   return util::to_gbc( get_feed_history().current_median_history, gbd );
}

void database::account_recovery_processing()
{
   // Clear expired recovery requests
   const auto& rec_req_idx = get_index< account_recovery_request_index >().indices().get< by_expiration >();
   auto rec_req = rec_req_idx.begin();

   while( rec_req != rec_req_idx.end() && rec_req->expires <= head_block_time() )
   {
      remove( *rec_req );
      rec_req = rec_req_idx.begin();
   }

   // Clear invalid historical authorities
   const auto& hist_idx = get_index< owner_authority_history_index >().indices(); //by id
   auto hist = hist_idx.begin();

   while( hist != hist_idx.end() && time_point_sec( hist->last_valid_time + GAMEBANK_OWNER_AUTH_RECOVERY_PERIOD ) < head_block_time() )
   {
      remove( *hist );
      hist = hist_idx.begin();
   }

   // Apply effective recovery_account changes
   const auto& change_req_idx = get_index< change_recovery_account_request_index >().indices().get< by_effective_date >();
   auto change_req = change_req_idx.begin();

   while( change_req != change_req_idx.end() && change_req->effective_on <= head_block_time() )
   {
      modify( get_account( change_req->account_to_recover ), [&]( account_object& a )
      {
         a.recovery_account = change_req->recovery_account;
      });

      remove( *change_req );
      change_req = change_req_idx.begin();
   }
}

void database::expire_escrow_ratification()
{
   const auto& escrow_idx = get_index< escrow_index >().indices().get< by_ratification_deadline >();
   auto escrow_itr = escrow_idx.lower_bound( false );

   while( escrow_itr != escrow_idx.end() && !escrow_itr->is_approved() && escrow_itr->ratification_deadline <= head_block_time() )
   {
      const auto& old_escrow = *escrow_itr;
      ++escrow_itr;

      adjust_balance( old_escrow.from, old_escrow.gbc_balance );
      adjust_balance( old_escrow.from, old_escrow.gbd_balance );
      adjust_balance( old_escrow.from, old_escrow.pending_fee );

      remove( old_escrow );
   }
}

void database::process_decline_voting_rights()
{
   const auto& request_idx = get_index< decline_voting_rights_request_index >().indices().get< by_effective_date >();
   auto itr = request_idx.begin();

   while( itr != request_idx.end() && itr->effective_date <= head_block_time() )
   {
      const auto& account = get< account_object, by_name >( itr->account );

      /// remove all current votes
      std::array<share_type, GAMEBANK_MAX_PROXY_RECURSION_DEPTH+1> delta;
      delta[0] = -account.vesting_shares.amount;
      for( int i = 0; i < GAMEBANK_MAX_PROXY_RECURSION_DEPTH; ++i )
         delta[i+1] = -account.proxied_vsf_votes[i];
      adjust_proxied_witness_votes( account, delta );

      clear_witness_votes( account );

      modify( account, [&]( account_object& a )
      {
         a.can_vote = false;
         a.proxy = GAMEBANK_PROXY_TO_SELF_ACCOUNT;
      });

      remove( *itr );
      itr = request_idx.begin();
   }
}

time_point_sec database::head_block_time()const
{
   return get_dynamic_global_properties().time;
}

uint32_t database::head_block_num()const
{
   return get_dynamic_global_properties().head_block_number;
}

block_id_type database::head_block_id()const
{
   return get_dynamic_global_properties().head_block_id;
}

node_property_object& database::node_properties()
{
   return _node_property_object;
}

uint32_t database::last_non_undoable_block_num() const
{
   return get_dynamic_global_properties().last_irreversible_block_num;
}

void database::initialize_evaluators()
{
   _my->_evaluator_registry.register_evaluator< vote_evaluator                           >();
   _my->_evaluator_registry.register_evaluator< comment_evaluator                        >();
   _my->_evaluator_registry.register_evaluator< comment_options_evaluator                >();
   _my->_evaluator_registry.register_evaluator< delete_comment_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< transfer_evaluator                       >();
   _my->_evaluator_registry.register_evaluator< transfer_to_vesting_evaluator            >();
   _my->_evaluator_registry.register_evaluator< withdraw_vesting_evaluator               >();
   _my->_evaluator_registry.register_evaluator< set_withdraw_vesting_route_evaluator     >();
   _my->_evaluator_registry.register_evaluator< account_create_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< account_update_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< witness_update_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< account_witness_vote_evaluator           >();
   _my->_evaluator_registry.register_evaluator< account_witness_proxy_evaluator          >();
   _my->_evaluator_registry.register_evaluator< custom_evaluator                         >();
   _my->_evaluator_registry.register_evaluator< custom_binary_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< custom_json_evaluator                    >();
   _my->_evaluator_registry.register_evaluator< pow_evaluator                            >();
   _my->_evaluator_registry.register_evaluator< pow2_evaluator                           >();
   _my->_evaluator_registry.register_evaluator< report_over_production_evaluator         >();
   _my->_evaluator_registry.register_evaluator< feed_publish_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< convert_evaluator                        >();
   _my->_evaluator_registry.register_evaluator< limit_order_create_evaluator             >();
   _my->_evaluator_registry.register_evaluator< limit_order_create2_evaluator            >();
   _my->_evaluator_registry.register_evaluator< limit_order_cancel_evaluator             >();
   _my->_evaluator_registry.register_evaluator< claim_account_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< create_claimed_account_evaluator         >();
   _my->_evaluator_registry.register_evaluator< request_account_recovery_evaluator       >();
   _my->_evaluator_registry.register_evaluator< recover_account_evaluator                >();
   _my->_evaluator_registry.register_evaluator< change_recovery_account_evaluator        >();
   _my->_evaluator_registry.register_evaluator< escrow_transfer_evaluator                >();
   _my->_evaluator_registry.register_evaluator< escrow_approve_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< escrow_dispute_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< escrow_release_evaluator                 >();
   _my->_evaluator_registry.register_evaluator< transfer_to_savings_evaluator            >();
   _my->_evaluator_registry.register_evaluator< transfer_from_savings_evaluator          >();
   _my->_evaluator_registry.register_evaluator< cancel_transfer_from_savings_evaluator   >();
   _my->_evaluator_registry.register_evaluator< decline_voting_rights_evaluator          >();
   _my->_evaluator_registry.register_evaluator< reset_account_evaluator                  >();
   _my->_evaluator_registry.register_evaluator< set_reset_account_evaluator              >();
   _my->_evaluator_registry.register_evaluator< claim_reward_balance_evaluator           >();
   _my->_evaluator_registry.register_evaluator< account_create_with_delegation_evaluator >();
   _my->_evaluator_registry.register_evaluator< delegate_vesting_shares_evaluator        >();
   _my->_evaluator_registry.register_evaluator< witness_set_properties_evaluator         >();
   _my->_evaluator_registry.register_evaluator< crowdfunding_evaluator                   >();
   _my->_evaluator_registry.register_evaluator< invest_evaluator                         >();
   _my->_evaluator_registry.register_evaluator< nonfungible_fund_create_evaluator        >();
   _my->_evaluator_registry.register_evaluator< nonfungible_fund_transfer_evaluator		 >();
   _my->_evaluator_registry.register_evaluator< nonfungible_fund_put_up_for_sale_evaluator        >();
   _my->_evaluator_registry.register_evaluator< nonfungible_fund_withdraw_from_sale_evaluator		 >();
   _my->_evaluator_registry.register_evaluator< nonfungible_fund_buy_evaluator		 >();
}


void database::set_custom_operation_interpreter( const std::string& id, std::shared_ptr< custom_operation_interpreter > registry )
{
   bool inserted = _custom_operation_interpreters.emplace( id, registry ).second;
   // This assert triggering means we're mis-configured (multiple registrations of custom JSON evaluator for same ID)
   FC_ASSERT( inserted );
}

std::shared_ptr< custom_operation_interpreter > database::get_custom_json_evaluator( const std::string& id )
{
   auto it = _custom_operation_interpreters.find( id );
   if( it != _custom_operation_interpreters.end() )
      return it->second;
   return std::shared_ptr< custom_operation_interpreter >();
}

void database::initialize_indexes()
{
   add_core_index< dynamic_global_property_index           >(*this);
   add_core_index< account_index                           >(*this);
   add_core_index< account_authority_index                 >(*this);
   add_core_index< witness_index                           >(*this);
   add_core_index< transaction_index                       >(*this);
   add_core_index< block_summary_index                     >(*this);
   add_core_index< witness_schedule_index                  >(*this);
   add_core_index< comment_index                           >(*this);
   add_core_index< comment_content_index                   >(*this);
   add_core_index< comment_vote_index                      >(*this);
   add_core_index< witness_vote_index                      >(*this);
   add_core_index< limit_order_index                       >(*this);
   add_core_index< feed_history_index                      >(*this);
   add_core_index< convert_request_index                   >(*this);
   add_core_index< liquidity_reward_balance_index          >(*this);
   add_core_index< operation_index                         >(*this);
   add_core_index< account_history_index                   >(*this);
   add_core_index< hardfork_property_index                 >(*this);
   add_core_index< withdraw_vesting_route_index            >(*this);
   add_core_index< owner_authority_history_index           >(*this);
   add_core_index< account_recovery_request_index          >(*this);
   add_core_index< change_recovery_account_request_index   >(*this);
   add_core_index< escrow_index                            >(*this);
   add_core_index< savings_withdraw_index                  >(*this);
   add_core_index< decline_voting_rights_request_index     >(*this);
   add_core_index< reward_fund_index                       >(*this);
   add_core_index< vesting_delegation_index                >(*this);
   add_core_index< vesting_delegation_expiration_index     >(*this);
   add_core_index< crowdfunding_index                      >(*this);
   add_core_index< crowdfunding_content_index              >(*this);
   add_core_index< crowdfunding_invest_index               >(*this);
   add_core_index< nonfungible_fund_index				   >(*this);
   add_core_index< nonfungible_fund_on_sale_index				   >(*this);

   _plugin_index_signal();
}

const std::string& database::get_json_schema()const
{
   return _json_schema;
}

void database::init_schema()
{
   /*done_adding_indexes();

   db_schema ds;

   std::vector< std::shared_ptr< abstract_schema > > schema_list;

   std::vector< object_schema > object_schemas;
   get_object_schemas( object_schemas );

   for( const object_schema& oschema : object_schemas )
   {
      ds.object_types.emplace_back();
      ds.object_types.back().space_type.first = oschema.space_id;
      ds.object_types.back().space_type.second = oschema.type_id;
      oschema.schema->get_name( ds.object_types.back().type );
      schema_list.push_back( oschema.schema );
   }

   std::shared_ptr< abstract_schema > operation_schema = get_schema_for_type< operation >();
   operation_schema->get_name( ds.operation_type );
   schema_list.push_back( operation_schema );

   for( const std::pair< std::string, std::shared_ptr< custom_operation_interpreter > >& p : _custom_operation_interpreters )
   {
      ds.custom_operation_types.emplace_back();
      ds.custom_operation_types.back().id = p.first;
      schema_list.push_back( p.second->get_operation_schema() );
      schema_list.back()->get_name( ds.custom_operation_types.back().type );
   }

   graphene::db::add_dependent_schemas( schema_list );
   std::sort( schema_list.begin(), schema_list.end(),
      []( const std::shared_ptr< abstract_schema >& a,
          const std::shared_ptr< abstract_schema >& b )
      {
         return a->id < b->id;
      } );
   auto new_end = std::unique( schema_list.begin(), schema_list.end(),
      []( const std::shared_ptr< abstract_schema >& a,
          const std::shared_ptr< abstract_schema >& b )
      {
         return a->id == b->id;
      } );
   schema_list.erase( new_end, schema_list.end() );

   for( std::shared_ptr< abstract_schema >& s : schema_list )
   {
      std::string tname;
      s->get_name( tname );
      FC_ASSERT( ds.types.find( tname ) == ds.types.end(), "types with different ID's found for name ${tname}", ("tname", tname) );
      std::string ss;
      s->get_str_schema( ss );
      ds.types.emplace( tname, ss );
   }

   _json_schema = fc::json::to_string( ds );
   return;*/
}

void database::init_genesis( uint64_t init_supply )
{
   try
   {
      struct auth_inhibitor
      {
         auth_inhibitor(database& db) : db(db), old_flags(db.node_properties().skip_flags)
         { db.node_properties().skip_flags |= skip_authority_check; }
         ~auth_inhibitor()
         { db.node_properties().skip_flags = old_flags; }
      private:
         database& db;
         uint32_t old_flags;
      } inhibitor(*this);

      // Create blockchain accounts
      public_key_type      init_public_key(GAMEBANK_INIT_PUBLIC_KEY);

//创建各类特殊账户
//account_object与account_authority_object

	//1矿工账号，当前witness
      create< account_object >( [&]( account_object& a )
      {
         a.name = GAMEBANK_MINER_ACCOUNT;
      } );
      create< account_authority_object >( [&]( account_authority_object& auth )
      {
         auth.account = GAMEBANK_MINER_ACCOUNT;
         auth.owner.weight_threshold = 1;
         auth.active.weight_threshold = 1;
      });


      create< account_object >( [&]( account_object& a )
      {
         a.name = GAMEBANK_NULL_ACCOUNT;
      } );
      create< account_authority_object >( [&]( account_authority_object& auth )
      {
         auth.account = GAMEBANK_NULL_ACCOUNT;
         auth.owner.weight_threshold = 1;
         auth.active.weight_threshold = 1;
      });

      create< account_object >( [&]( account_object& a )
      {
         a.name = GAMEBANK_TEMP_ACCOUNT;
      } );
      create< account_authority_object >( [&]( account_authority_object& auth )
      {
         auth.account = GAMEBANK_TEMP_ACCOUNT;
         auth.owner.weight_threshold = 0;
         auth.active.weight_threshold = 0;
      });

      for( int i = 0; i < GAMEBANK_NUM_INIT_MINERS; ++i )
      {
         create< account_object >( [&]( account_object& a )
         {
         //函数执行
            a.name = GAMEBANK_INIT_MINER_NAME + ( i ? fc::to_string( i ) : std::string() );
            a.memo_key = init_public_key;
            a.balance  = asset( i ? 0 : init_supply, GBC_SYMBOL );
         } );

         create< account_authority_object >( [&]( account_authority_object& auth )
         {
            auth.account = GAMEBANK_INIT_MINER_NAME + ( i ? fc::to_string( i ) : std::string() );
            auth.owner.add_authority( init_public_key, 1 );
            auth.owner.weight_threshold = 1;
            auth.active  = auth.owner;
            auth.posting = auth.active;
         });

         create< witness_object >( [&]( witness_object& w )
         {
            w.owner        = GAMEBANK_INIT_MINER_NAME + ( i ? fc::to_string(i) : std::string() );
            w.signing_key  = init_public_key;
            w.schedule = witness_object::miner;
         } );
      }

//@see dynamic_global_property_object
      create< dynamic_global_property_object >( [&]( dynamic_global_property_object& p )
      {
         p.current_witness = GAMEBANK_INIT_MINER_NAME;
         p.time = GAMEBANK_GENESIS_TIME;
         p.recent_slots_filled = fc::uint128::max_value();
         p.participation_count = 128;
         p.current_supply = asset( init_supply, GBC_SYMBOL );
         p.virtual_supply = p.current_supply;
         p.maximum_block_size = GAMEBANK_MAX_BLOCK_SIZE;
      } );

      // Nothing to do
      create< feed_history_object >( [&]( feed_history_object& o ) {});
      for( int i = 0; i < 0x10000; i++ )
         create< block_summary_object >( [&]( block_summary_object& ) {});
      create< hardfork_property_object >( [&](hardfork_property_object& hpo )
      {
         hpo.processed_hardforks.push_back( GAMEBANK_GENESIS_TIME );
      } );

      // Create witness scheduler
      create< witness_schedule_object >( [&]( witness_schedule_object& wso )
      {
         wso.current_shuffled_witnesses[0] = GAMEBANK_INIT_MINER_NAME;
      } );
   }
   FC_CAPTURE_AND_RETHROW()
}


void database::validate_transaction( const signed_transaction& trx )
{
   database::with_write_lock( [&]()
   {
      auto session = start_undo_session();
      _apply_transaction( trx );
      session.undo();
   });
}

void database::notify_changed_objects()
{
   try
   {
      /*vector< chainbase::generic_id > ids;
      get_changed_ids( ids );
      GAMEBANK_TRY_NOTIFY( changed_objects, ids )*/
      /*
      if( _undo_db.enabled() )
      {
         const auto& head_undo = _undo_db.head();
         vector<object_id_type> changed_ids;  changed_ids.reserve(head_undo.old_values.size());
         for( const auto& item : head_undo.old_values ) changed_ids.push_back(item.first);
         for( const auto& item : head_undo.new_ids ) changed_ids.push_back(item);
         vector<const object*> removed;
         removed.reserve( head_undo.removed.size() );
         for( const auto& item : head_undo.removed )
         {
            changed_ids.push_back( item.first );
            removed.emplace_back( item.second.get() );
         }
         GAMEBANK_TRY_NOTIFY( changed_objects, changed_ids )
      }
      */
   }
   FC_CAPTURE_AND_RETHROW()

}

void database::set_flush_interval( uint32_t flush_blocks )
{
   _flush_blocks = flush_blocks;
   _next_flush_block = 0;
}

//////////////////// private methods ////////////////////
//区块入链
//push_block -> _push_block -> apply_block -> _apply_block
void database::apply_block( const signed_block& next_block, uint32_t skip )
{ try {
   //fc::time_point begin_time = fc::time_point::now();

   auto block_num = next_block.block_num();
   if( _checkpoints.size() && _checkpoints.rbegin()->second != block_id_type() )
   {
      auto itr = _checkpoints.find( block_num );
      if( itr != _checkpoints.end() )
         FC_ASSERT( next_block.id() == itr->second, "Block did not match checkpoint", ("checkpoint",*itr)("block_id",next_block.id()) );

      if( _checkpoints.rbegin()->first >= block_num )
         skip = skip_witness_signature
              | skip_transaction_signatures
              | skip_transaction_dupe_check
              | skip_fork_db
              | skip_block_size_check
              | skip_tapos_check
              | skip_authority_check
              /* | skip_merkle_check While blockchain is being downloaded, txs need to be validated against block headers */
              | skip_undo_history_check
              | skip_witness_schedule_check
              | skip_validate
              | skip_validate_invariants
              ;
   }

   detail::with_skip_flags( *this, skip, [&]()
   {
   //// 在此调用实际的添加区块方法
      _apply_block( next_block );
   } );

   /*try
   {
   /// check invariants
   if( is_producing() || !( skip & skip_validate_invariants ) )
      validate_invariants();
   }
   FC_CAPTURE_AND_RETHROW( (next_block) );*/

   //fc::time_point end_time = fc::time_point::now();
   //fc::microseconds dt = end_time - begin_time;
   if( _flush_blocks != 0 )
   {
      if( _next_flush_block == 0 )
      {
         uint32_t lep = block_num + 1 + _flush_blocks * 9 / 10;
         uint32_t rep = block_num + 1 + _flush_blocks;

         // use time_point::now() as RNG source to pick block randomly between lep and rep
         //随机从lep ~ rep选择一个区块
         uint32_t span = rep - lep;
         uint32_t x = lep;
         if( span > 0 )
         {
            uint64_t now = uint64_t( fc::time_point::now().time_since_epoch().count() );
            x += now % span;
         }
         _next_flush_block = x;
         //ilog( "Next flush scheduled at block ${b}", ("b", x) );
      }

      if( _next_flush_block == block_num )
      {
        //如果“命中”? 下一次重选，这一次对database进行flush
         _next_flush_block = 0;
         //ilog( "Flushing database shared memory at block ${b}", ("b", block_num) );
         chainbase::database::flush();
      }
   }

} FC_CAPTURE_AND_RETHROW( (next_block) ) }

void database::check_free_memory( bool force_print, uint32_t current_block_num )
{
   uint64_t free_mem = get_free_memory();
   uint64_t max_mem = get_max_memory();

   if( BOOST_UNLIKELY( _shared_file_full_threshold != 0 && _shared_file_scale_rate != 0 && free_mem < ( ( uint128_t( GAMEBANK_100_PERCENT - _shared_file_full_threshold ) * max_mem ) / GAMEBANK_100_PERCENT ).to_uint64() ) )
   {
      uint64_t new_max = ( uint128_t( max_mem * _shared_file_scale_rate ) / GAMEBANK_100_PERCENT ).to_uint64() + max_mem;

      wlog( "Memory is almost full, increasing to ${mem}M", ("mem", new_max / (1024*1024)) );

      resize( new_max );

      uint32_t free_mb = uint32_t( get_free_memory() / (1024*1024) );
      wlog( "Free memory is now ${free}M", ("free", free_mb) );
      _last_free_gb_printed = free_mb / 1024;
   }
   else
   {
      uint32_t free_gb = uint32_t( free_mem / (1024*1024*1024) );
      if( BOOST_UNLIKELY( force_print || (free_gb < _last_free_gb_printed) || (free_gb > _last_free_gb_printed+1) ) )
      {
         ilog( "Free memory is now ${n}G. Current block number: ${block}", ("n", free_gb)("block",current_block_num) );
         _last_free_gb_printed = free_gb;
      }

      if( BOOST_UNLIKELY( free_gb == 0 ) )
      {
         uint32_t free_mb = uint32_t( free_mem / (1024*1024) );

   #ifdef IS_TEST_NET
      if( !disable_low_mem_warning )
   #endif
         if( free_mb <= 100 && head_block_num() % 10 == 0 )
            elog( "Free memory is now ${n}M. Increase shared file size immediately!" , ("n", free_mb) );
      }
   }
}

//当前已经切换到最长链,next_block也保存在forkdb
// 1 验证区块的merkle_root
// 2 验证block_header
// 3 验证区块尺寸
// 4 更新全局属性中的当前生产区块的见证人
// 5 apply区块中的所有交易
// 6 持久化，更新全局等其他操作.......
void database::_apply_block( const signed_block& next_block )
{ try {

   //block_notification成员: id,num,signed_block
   block_notification note( next_block );

   notify_pre_apply_block( note );	// _dupe_customs.clear();？

   const uint32_t next_block_num = note.block_num;

   BOOST_SCOPE_EXIT( this_ )
   {
      this_->_currently_processing_block_id.reset();
   } BOOST_SCOPE_EXIT_END
   _currently_processing_block_id = note.block_id;

   uint32_t skip = get_node_properties().skip_flags;

   _current_block_num    = next_block_num;
   _current_trx_in_block = 0;
   _current_virtual_op   = 0;

   //???
   if( BOOST_UNLIKELY( next_block_num == 1 ) )
   {
      //
      apply_pre_genesis_patches();
      
      // For every existing before the head_block_time (genesis time), apply the hardfork
      // This allows the test net to launch with past hardforks and apply the next harfork when running

      uint32_t n;
      for( n=0; n<GAMEBANK_NUM_HARDFORKS; n++ )
      {
         if( _hardfork_times[n+1] > next_block.timestamp )
            break;
      }

      if( n > 0 )
      {
         ilog( "Processing ${n} genesis hardforks", ("n", n) );
         set_hardfork( n, true );

         const hardfork_property_object& hardfork_state = get_hardfork_property_object();
         FC_ASSERT( hardfork_state.current_hardfork_version == _hardfork_versions[n], "Unexpected genesis hardfork state" );

         const auto& witness_idx = get_index<witness_index>().indices().get<by_id>();
         vector<witness_id_type> wit_ids_to_update;
         for( auto it=witness_idx.begin(); it!=witness_idx.end(); ++it )
            wit_ids_to_update.push_back(it->id);

         for( witness_id_type wit_id : wit_ids_to_update )
         {
            modify( get( wit_id ), [&]( witness_object& wit )
            {
               wit.running_version = _hardfork_versions[n];
               wit.hardfork_version_vote = _hardfork_versions[n];
               wit.hardfork_time_vote = _hardfork_times[n];
            } );
         }
      }
   }

   //验证区块的merkle_root
   if( !( skip & skip_merkle_check ) )
   {
      auto merkle_root = next_block.calculate_merkle_root();

      try
      {
         FC_ASSERT( next_block.transaction_merkle_root == merkle_root, "Merkle check failed", ("next_block.transaction_merkle_root",next_block.transaction_merkle_root)("calc",merkle_root)("next_block",next_block)("id",next_block.id()) );
      }
      catch( fc::assert_exception& e )
      {
         const auto& merkle_map = get_shared_db_merkle();
         auto itr = merkle_map.find( next_block_num );

         if( itr == merkle_map.end() || itr->second != merkle_root )
            throw e;
      }
   }

   //验证block_header,返回db中的witness_object
   const witness_object& signing_witness = validate_block_header(skip, next_block);

   const auto& gprops = get_dynamic_global_properties();
   //验证区块尺寸
   auto block_size = fc::raw::pack_size( next_block );

   FC_ASSERT( block_size <= gprops.maximum_block_size, "Block Size is too Big", ("next_block_num",next_block_num)("block_size", block_size)("max",gprops.maximum_block_size) );

   if( block_size < GAMEBANK_MIN_BLOCK_SIZE )
   {
      elog( "Block size is too small",
         ("next_block_num",next_block_num)("block_size", block_size)("min",GAMEBANK_MIN_BLOCK_SIZE)
      );
   }

   /// modify current witness so transaction evaluators can know who included the transaction,
   /// this is mostly for POW operations which must pay the current_witness
   //  更新全局属性中的当前生产区块的见证人
   //  dynamic_global_property_object中的current_witness需实时更新为区块生产者，以便pow operations支付收益
   modify( gprops, [&]( dynamic_global_property_object& dgp ){
      dgp.current_witness = next_block.witness;
   });

   /// parse witness version reporting
   process_header_extensions( next_block );

      const auto& witness = get_witness( next_block.witness );
      const auto& hardfork_state = get_hardfork_property_object();
      FC_ASSERT( witness.running_version >= hardfork_state.current_hardfork_version,
         "Block produced by witness that is not running current hardfork",
         ("witness",witness)("next_block.witness",next_block.witness)("hardfork_state", hardfork_state)
      );

   //apply区块中的所有交易
   for( const auto& trx : next_block.transactions )
   {
      /* We do not need to push the undo state for each transaction
       * because they either all apply and are valid or the
       * entire block fails to apply.  We only need an "undo" state
       * for transactions when validating broadcast transactions or
       * when building a block.
       */
      apply_transaction( trx, skip );
      ++_current_trx_in_block;
   }

   _current_trx_in_block = -1;
   _current_op_in_trx = 0;
   _current_virtual_op = 0;

   //更新全局属性，并判断是否有见证人未能正常出块
   update_global_dynamic_data(next_block);

   //更新见证人出块记录  
   update_signing_witness(signing_witness, next_block);

   //写入block log持久化
   update_last_irreversible_block();

   //for TaPos
   create_block_summary(next_block);
   //移除db中已过期的所有交易
   clear_expired_transactions();
   clear_expired_orders();
   clear_expired_nonfungible_funds_on_sale();
   reclaim_account_creation_delegations();
   clear_expired_delegations();
   //调用洗牌算法
   update_witness_schedule(*this);

   //更新历史喂价中间值，用于确定GBD->GBC的汇率（每小时更新一次）
   update_median_feed();

   //更新系统STEE总量价值，根据债务所有权比率，确定sbd_print_rate
   update_virtual_supply();

   clear_null_account_balance();
   //每次生成新的区块，会根据当前通胀率新增GBC，process_funds对新增GBC的分配策略
   process_funds();
   //处理所有转换请求（GBD->GBC）
   process_conversions();
   process_comment_cashout();
   process_vesting_withdrawals();
   process_savings_withdraws();
   process_crowdfunding();
   pay_liquidity_reward();

   //上面的操作变更了virtual_supply和current_sbd，再次更新
   update_virtual_supply();

   account_recovery_processing();
   expire_escrow_ratification();
   process_decline_voting_rights();

   process_hardforks();

   // notify observers that the block has been applied
   notify_post_apply_block( note );

   notify_changed_objects();
} //FC_CAPTURE_AND_RETHROW( (next_block.block_num()) )  }
FC_CAPTURE_LOG_AND_RETHROW( (next_block.block_num()) )
}

struct process_header_visitor
{
   process_header_visitor( const std::string& witness, database& db ) : _witness( witness ), _db( db ) {}

   typedef void result_type;

   const std::string& _witness;
   database& _db;

   void operator()( const void_t& obj ) const
   {
      //Nothing to do.
   }

   void operator()( const version& reported_version ) const
   {
      const auto& signing_witness = _db.get_witness( _witness );
      //idump( (next_block.witness)(signing_witness.running_version)(reported_version) );

      if( reported_version != signing_witness.running_version )
      {
         _db.modify( signing_witness, [&]( witness_object& wo )
         {
            wo.running_version = reported_version;
         });
      }
   }

   void operator()( const hardfork_version_vote& hfv ) const
   {
      const auto& signing_witness = _db.get_witness( _witness );
      //idump( (next_block.witness)(signing_witness.running_version)(hfv) );

      if( hfv.hf_version != signing_witness.hardfork_version_vote || hfv.hf_time != signing_witness.hardfork_time_vote )
         _db.modify( signing_witness, [&]( witness_object& wo )
         {
            wo.hardfork_version_vote = hfv.hf_version;
            wo.hardfork_time_vote = hfv.hf_time;
         });
   }

   template<typename T>
   void operator()( const T& unknown_obj ) const
   {
      FC_ASSERT( false, "Unknown extension in block header" );
   }
};

void database::process_header_extensions( const signed_block& next_block )
{
   process_header_visitor _v( next_block.witness, *this );

   for( const auto& e : next_block.extensions )
      e.visit( _v );
}
//@see feed_history_object
//更新喂价中间值用于确定GBD->GBC的兑换，仅在_apply_block中被调用（每一个见证人生成新区块时）
//记录过去一周的历史价格，并且每小时更新一次，结果保存在feed_history_object
void database::update_median_feed() {
try {
//这里设置了每小时更新一次
   if( (head_block_num() % GAMEBANK_FEED_INTERVAL_BLOCKS) != 0 )
      return;

//以下每小时做一次：

   auto now = head_block_time();
   const witness_schedule_object& wso = get_witness_schedule_object();
   vector<price> feeds; feeds.reserve( wso.num_scheduled_witnesses );
   //只有见证人可以发布喂价，遍历当前round的21个见证人，获取每个见证人发布的喂价
   for( int i = 0; i < wso.num_scheduled_witnesses; i++ )
   {
      const auto& wit = get_witness( wso.current_shuffled_witnesses[i] );
		//只获取7天以内的发布的喂价
         if( now < wit.last_gbd_exchange_update + GAMEBANK_MAX_FEED_AGE_SECONDS
            && !wit.gbd_exchange_rate.is_null() )
         {
            feeds.push_back( wit.gbd_exchange_rate );
         }

   }

	//保证至少有7个见证人发布的喂价有效
   if( feeds.size() >= GAMEBANK_MIN_FEEDS )
   {
      //排序所有喂价，取中间值
      std::sort( feeds.begin(), feeds.end() );
      auto median_feed = feeds[feeds.size()/2];

	  //更新feed_history_object
      modify( get_feed_history(), [&]( feed_history_object& fho )
      {
         fho.price_history.push_back( median_feed );
         size_t gamebank_feed_history_window = 24 * 7; // 7 days * 24 hours per day

         gamebank_feed_history_window = GAMEBANK_FEED_HISTORY_WINDOW;

		//如果保存的历史数量超过7天*24，则删除最旧的
         if( fho.price_history.size() > gamebank_feed_history_window )
            fho.price_history.pop_front();

         if( fho.price_history.size() )
         {
            /// BW-TODO Why deque is used here ? Also why don't make copy of whole container ?
            std::deque< price > copy;
            for( const auto& i : fho.price_history )
            {
               copy.push_back( i );
            }

            std::sort( copy.begin(), copy.end() ); /// TODO: use nth_item
            //计算历史中间价
            fho.current_median_history = copy[copy.size()/2];

#ifdef IS_TEST_NET
            if( skip_price_feed_limit_check )
               return;
#endif

               const auto& gpo = get_dynamic_global_properties();
			   //白皮书--Sustainable Debt-to-Ownership Ratios--限制债务所有权比值不超过10%的实现
			   // This price limits GBD to 10% market cap
			   //market cap:市场的总价值
			   //current_sbd_supply：系统当前GBD发行总量
			   //current_supply:系统当前GBC发行总量
			   //债务所有权比： GBD价值/市场GBC总价值
			   //min_price：在这里不是代表汇率，而是设置限制
               /*
			   为确保GBD的总量最多不超过GBC市场总价值的10%：
			   设CS为GBC总量
			   设CSB为GBD总量
			   设P1为GBD可兑换多少个GBC
			   设P2为GBC到USD doller的市场汇率
			   则P1 = 1/P2
			   市场GBC的总价值为: (CS + CSB*P1) * P2 dollers
			   GBD价值为(CSB * P1 * P2) dollers
			   需满足GBD总量低于市场总价值的10%：
			   CSB * P1 * P2 / (CS + CSB*P1) * P2 <= 0.1 
			   ==> 9 * CSB * P1 <= CS
			   ===> P1 <= CS / (9*CSB)
			   即GBD最多可兑换的GBC必须小于等于GBC总量/(GBD总量*9)
			   */			    
			   //price限制： 9 * GBD总量 可兑换 GBC总量
               price min_price( asset( 9 * gpo.current_gbd_supply.amount, GBD_SYMBOL ), gpo.current_supply ); // This price limits GBD to 10% market cap
				//price较大者，base可兑换的quote更少；price较低者，base可兑换的quote更多
               if( min_price > fho.current_median_history )
			   	  //当前current_median_history将使GBD兑换的GBC数量超过min_price设置的限制
			   	  //降低GBD可兑换的GBC数量，将降低GBD债务价值(类似于GBD主动贬值)，进一步降低债务所有权比值
			   	  //直至把债务所有权比降到达到gamebank区块链市场上限的10%
                  fho.current_median_history = min_price;

         }
      });
   }
} FC_CAPTURE_AND_RETHROW() }

void database::apply_transaction(const signed_transaction& trx, uint32_t skip)
{
   detail::with_skip_flags( *this, skip, [&]() { _apply_transaction(trx); });
}

// push_transaction -> _push_transaction -> _apply_transaction ->  apply_operation -> do_apply( op )
//  _apply_block  - > apply_transaction  -> _apply_transaction
//                      _generate_block  -> _apply_transaction
//                  validate_transaction -> _apply_transaction

//1 验证交易中所有操作权限
//2 验证交易是否在transaction_index（不应该在）
//3 验证交易签名,权限
//4 创建transaction_object, 放入transaction_index
//5 更新账户带宽
//6 do_apply交易中包含的所有operations
void database::_apply_transaction(const signed_transaction& trx)
{ try {
   transaction_notification note(trx);
   _current_trx_id = note.transaction_id;
   const transaction_id_type& trx_id = note.transaction_id;
   _current_virtual_op = 0;

   uint32_t skip = get_node_properties().skip_flags;

   //operation_validate
   //验证交易中所有操作
   if( !(skip&skip_validate) )   /* issue #505 explains why this skip_flag is disabled */
      trx.validate();

   auto& trx_idx = get_index<transaction_index>();
   const chain_id_type& chain_id = get_chain_id();
   // idump((trx_id)(skip&skip_transaction_dupe_check));
   //当前交易不应该在trx_idx中
   FC_ASSERT( (skip & skip_transaction_dupe_check) ||
              trx_idx.indices().get<by_trx_id>().find(trx_id) == trx_idx.indices().get<by_trx_id>().end(),
              "Duplicate transaction check failed", ("trx_ix", trx_id) );

	//验证交易签名，权限验证
   if( !(skip & (skip_transaction_signatures | skip_authority_check) ) )
   {
      auto get_active  = [&]( const string& name ) { return authority( get< account_authority_object, by_account >( name ).active ); };
      auto get_owner   = [&]( const string& name ) { return authority( get< account_authority_object, by_account >( name ).owner );  };
      auto get_posting = [&]( const string& name ) { return authority( get< account_authority_object, by_account >( name ).posting );  };

      try
      {
		//验证交易签名,权限
		//call gamebank::protocol::verify_authority
         trx.verify_authority( chain_id, get_active, get_owner, get_posting, GAMEBANK_MAX_SIG_CHECK_DEPTH );
      }
      catch( protocol::tx_missing_active_auth& e )
      {
      	//交易验证缺少active权限
      	//？？
         if( get_shared_db_merkle().find( head_block_num() + 1 ) == get_shared_db_merkle().end() )
            throw e;
      }
   }

   //Skip all manner of expiration and TaPoS checking if we're on block 1; It's impossible that the transaction is
   //expired, and TaPoS makes no sense as no blocks exist.
   //@see block_summary_object
   if( BOOST_LIKELY(head_block_num() > 0) )
   {
      if( !(skip & skip_tapos_check) )
      {
         //每新生产一个区块，对创建block_summary_object
         const auto& tapos_block_summary = get< block_summary_object >( trx.ref_block_num );
         //Verify TaPoS block summary has correct ID prefix, and that this block's time is not past the expiration
		//@see sign_transaction and @see set_reference_block
         GAMEBANK_ASSERT( trx.ref_block_prefix == tapos_block_summary.block_id._hash[1], transaction_tapos_exception,
                    "", ("trx.ref_block_prefix", trx.ref_block_prefix)
                    ("tapos_block_summary",tapos_block_summary.block_id._hash[1]));
      }

      fc::time_point_sec now = head_block_time();
		//交易的过期时间应该在未来1个小时以内
      GAMEBANK_ASSERT( trx.expiration <= now + fc::seconds(GAMEBANK_MAX_TIME_UNTIL_EXPIRATION), transaction_expiration_exception,
                  "", ("trx.expiration",trx.expiration)("now",now)("max_til_exp",GAMEBANK_MAX_TIME_UNTIL_EXPIRATION));

      GAMEBANK_ASSERT( now < trx.expiration, transaction_expiration_exception, "", ("now",now)("trx.exp",trx.expiration) );
      GAMEBANK_ASSERT( now <= trx.expiration, transaction_expiration_exception, "", ("now",now)("trx.exp",trx.expiration) );
   }

   //Insert transaction into unique transactions database.
   //将验证过权限的交易和签名合法的交易放入transaction_index
   if( !(skip & skip_transaction_dupe_check) )
   {
      create<transaction_object>([&](transaction_object& transaction) {
         transaction.trx_id = trx_id;
         transaction.expiration = trx.expiration;
         fc::raw::pack_to_buffer( transaction.packed_trx, trx );
      });
   }
   //@see witness_plugin_impl::on_pre_apply_transaction
   //更新db中账户带宽相关（因为每次操作均会消耗相应的账号带宽）
   notify_pre_apply_transaction( note );

   //Finally process the operations
   _current_op_in_trx = 0;

   //do_apply交易中包含的所有operations
   for( const auto& op : trx.operations )
   { try {
      apply_operation(op);
      ++_current_op_in_trx;
     } FC_CAPTURE_AND_RETHROW( (op) );
   }
   _current_trx_id = transaction_id_type();

   notify_post_apply_transaction( note );

} FC_CAPTURE_AND_RETHROW( (trx) ) }

//处理交易中的每一个操作
void database::apply_operation(const operation& op)
{
   operation_notification note(op);
   notify_pre_apply_operation( note );

   if( _benchmark_dumper.is_enabled() )
      _benchmark_dumper.begin();

   //执行操作
   _my->_evaluator_registry.get_evaluator( op ).apply( op );

   if( _benchmark_dumper.is_enabled() )
      _benchmark_dumper.end< true/*APPLY_CONTEXT*/ >( _my->_evaluator_registry.get_evaluator( op ).get_name( op ) );

	//not used
   notify_post_apply_operation( note );
}


template <typename TFunction> struct fcall {};

template <typename TResult, typename... TArgs>
struct fcall<TResult(TArgs...)>
{
   using TNotification = std::function<TResult(TArgs...)>;

   fcall() = default;
   fcall(const TNotification& func, util::advanced_benchmark_dumper& dumper,
         const abstract_plugin& plugin, const std::string& item_name)
         : _func(func), _benchmark_dumper(dumper)
      {
         _name = plugin.get_name() + item_name;
      }

   void operator () (TArgs&&... args)
   {
      if (_benchmark_dumper.is_enabled())
         _benchmark_dumper.begin();

      _func(std::forward<TArgs>(args)...);

      if (_benchmark_dumper.is_enabled())
         _benchmark_dumper.end(_name);
   }

private:
   TNotification                    _func;
   util::advanced_benchmark_dumper& _benchmark_dumper;
   std::string                      _name;
};

template <typename TResult, typename... TArgs>
struct fcall<std::function<TResult(TArgs...)>>
   : public fcall<TResult(TArgs...)>
{
   typedef fcall<TResult(TArgs...)> TBase;
   using TBase::TBase;
};

template <typename TSignal, typename TNotification>
boost::signals2::connection database::connect_impl( TSignal& signal, const TNotification& func,
   const abstract_plugin& plugin, int32_t group, const std::string& item_name )
{
   fcall<TNotification> fcall_wrapper(func,_benchmark_dumper,plugin,item_name);

   return signal.connect(group, fcall_wrapper);
}

template< bool IS_PRE_OPERATION >
boost::signals2::connection database::any_apply_operation_handler_impl( const apply_operation_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   auto complex_func = [this, func, &plugin]( const operation_notification& o )
   {
      std::string name;

      if (_benchmark_dumper.is_enabled())
      {
         if( _my->_evaluator_registry.is_evaluator( o.op ) )
            name = _benchmark_dumper.generate_desc< IS_PRE_OPERATION >( plugin.get_name(), _my->_evaluator_registry.get_evaluator( o.op ).get_name( o.op ) );
         else
            name = util::advanced_benchmark_dumper::get_virtual_operation_name();

         _benchmark_dumper.begin();
      }

      func( o );

      if (_benchmark_dumper.is_enabled())
         _benchmark_dumper.end( name );
   };

   if( IS_PRE_OPERATION )
      return _pre_apply_operation_signal.connect(group, complex_func);
   else
      return _post_apply_operation_signal.connect(group, complex_func);
}

boost::signals2::connection database::add_pre_apply_operation_handler( const apply_operation_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return any_apply_operation_handler_impl< true/*IS_PRE_OPERATION*/ >( func, plugin, group );
}

boost::signals2::connection database::add_post_apply_operation_handler( const apply_operation_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return any_apply_operation_handler_impl< false/*IS_PRE_OPERATION*/ >( func, plugin, group );
}

boost::signals2::connection database::add_pre_apply_transaction_handler( const apply_transaction_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return connect_impl(_pre_apply_transaction_signal, func, plugin, group, "->transaction");
}

boost::signals2::connection database::add_post_apply_transaction_handler( const apply_transaction_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return connect_impl(_post_apply_transaction_signal, func, plugin, group, "<-transaction");
}

boost::signals2::connection database::add_pre_apply_block_handler( const apply_block_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return connect_impl(_pre_apply_block_signal, func, plugin, group, "->block");
}

boost::signals2::connection database::add_post_apply_block_handler( const apply_block_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return connect_impl(_post_apply_block_signal, func, plugin, group, "<-block");
}

boost::signals2::connection database::add_irreversible_block_handler( const irreversible_block_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return connect_impl(_on_irreversible_block, func, plugin, group, "<-irreversible");
}

boost::signals2::connection database::add_pre_reindex_handler(const reindex_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return connect_impl(_pre_reindex_signal, func, plugin, group, "->reindex");
}

boost::signals2::connection database::add_post_reindex_handler(const reindex_handler_t& func,
   const abstract_plugin& plugin, int32_t group )
{
   return connect_impl(_post_reindex_signal, func, plugin, group, "<-reindex");
}


//@see _apply_block
//验证新区块的block_header
const witness_object& database::validate_block_header( uint32_t skip, const signed_block& next_block )const
{ try {
   //验证head_block_id
   FC_ASSERT( head_block_id() == next_block.previous, "", ("head_block_id",head_block_id())("next.prev",next_block.previous) );
   //验证timestamp
   FC_ASSERT( head_block_time() < next_block.timestamp, "", ("head_block_time",head_block_time())("next",next_block.timestamp)("blocknum",next_block.block_num()) );

   //见证人的signing_key是否能够验证新区块的签名
   const witness_object& witness = get_witness( next_block.witness );

   if( !(skip&skip_witness_signature) )
      FC_ASSERT( next_block.validate_signee( witness.signing_key ) );

   //验证当前生成区块的见证人是否是调度产生的见证人
   if( !(skip&skip_witness_schedule_check) )
   {
      uint32_t slot_num = get_slot_at_time( next_block.timestamp );
      FC_ASSERT( slot_num > 0 );

      string scheduled_witness = get_scheduled_witness( slot_num );

      FC_ASSERT( witness.owner == scheduled_witness, "Witness produced block at wrong time",
                 ("block witness",next_block.witness)("scheduled",scheduled_witness)("slot_num",slot_num) );
   }

   return witness;
} FC_CAPTURE_AND_RETHROW() }

void database::create_block_summary(const signed_block& next_block)
{ try {
   block_summary_id_type sid( next_block.block_num() & 0xffff );
   modify( get< block_summary_object >( sid ), [&](block_summary_object& p) {
         p.block_id = next_block.id();
   });
} FC_CAPTURE_AND_RETHROW() }

//@see _apply_block
//新生产的区块在入链前都会调用一次
void database::update_global_dynamic_data( const signed_block& b )
{ try {
   const dynamic_global_property_object& _dgp =
      get_dynamic_global_properties();

	  //以下处理见证人未能成功出块
   uint32_t missed_blocks = 0;
   if( head_block_time() != fc::time_point_sec() )
   {
      missed_blocks = get_slot_at_time( b.timestamp );
      assert( missed_blocks != 0 );
      missed_blocks--;
      for( uint32_t i = 0; i < missed_blocks; ++i )
      {
         const auto& witness_missed = get_witness( get_scheduled_witness( i + 1 ) );
         if(  witness_missed.owner != b.witness )
         {
            modify( witness_missed, [&]( witness_object& w )
            {
               w.total_missed++;

				   // 超过1天没有确认过区块了,则把该见证人shutdown
                  if( head_block_num() - w.last_confirmed_block_num  > GAMEBANK_BLOCKS_PER_DAY )
                  {
					//即这个见证人已有1天时间没有生产新的区块
                     w.signing_key = public_key_type();//清空签名公钥,从而使其无法生产区块
                     push_virtual_operation( shutdown_witness_operation( w.owner ) );
                  }
               
            } );
         }
      }
   }

   // dynamic global properties updating
   modify( _dgp, [&]( dynamic_global_property_object& dgp )
   {
      // This is constant time assuming 100% participation. It is O(B) otherwise (B = Num blocks between update)
      for( uint32_t i = 0; i < missed_blocks + 1; i++ )
      {
         dgp.participation_count -= dgp.recent_slots_filled.hi & 0x8000000000000000ULL ? 1 : 0;
         dgp.recent_slots_filled = ( dgp.recent_slots_filled << 1 ) + ( i == 0 ? 1 : 0 );
         dgp.participation_count += ( i == 0 ? 1 : 0 );
      }

	  //更新全局属性之最新入链的区块编号
      dgp.head_block_number = b.block_num();
      // Following FC_ASSERT should never fail, as _currently_processing_block_id is always set by caller
      FC_ASSERT( _currently_processing_block_id.valid() );
	  //即当前正在入链的区块id
      dgp.head_block_id = *_currently_processing_block_id;
      dgp.time = b.timestamp;
      dgp.current_aslot += missed_blocks+1;
   } );

   if( !(get_node_properties().skip_flags & skip_undo_history_check) )
   {
      GAMEBANK_ASSERT( _dgp.head_block_number - _dgp.last_irreversible_block_num  < GAMEBANK_MAX_UNDO_HISTORY, undo_database_exception,
                 "The database does not have enough undo history to support a blockchain with so many missed blocks. "
                 "Please add a checkpoint if you would like to continue applying blocks beyond this point.",
                 ("last_irreversible_block_num",_dgp.last_irreversible_block_num)("head", _dgp.head_block_number)
                 ("max_undo",GAMEBANK_MAX_UNDO_HISTORY) );
   }
} FC_CAPTURE_AND_RETHROW() }

//1 更新系统GBC总量价值
//2 根据债务所有权比率，更新sbd_print_rate
void database::update_virtual_supply()
{ try {
   modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& dgp )
   {
      //整个系统价值的gamebank总数（包括gbc的总数量 加上 GBD的总数量兑换成gbc的总数量）。
      dgp.virtual_supply = dgp.current_supply
         + ( get_feed_history().current_median_history.is_null() ? asset( 0, GBC_SYMBOL ) : dgp.current_gbd_supply * get_feed_history().current_median_history );

	  //当前feed
      auto median_price = get_feed_history().current_median_history;

      if( !median_price.is_null()              )
      {
		//债务所有权比率（GBD价值/系统GBC总价值）
         auto percent_gbd = uint16_t( ( ( fc::uint128_t( ( dgp.current_gbd_supply * get_feed_history().current_median_history ).amount.value ) * GAMEBANK_100_PERCENT )
            / dgp.virtual_supply.amount.value ).to_uint64() );
         //债务所有权比率影响GBD的发行。
         //更新sbd_print_rate
         //当债务所有权比率低于2%，增大GBD发行(在奖励结算时，50%的GBD全部按GBD结算, @see creat_sbd)
         //当债务所有权比率高于5%，停止GBD发行(在奖励结算时，50%的GBD全部按GBC结算, @see creat_sbd)
         //当债务高于2%小于5%时，降低（不是停止）GBD发行(在奖励结算时，50%的GBD全部按合适的比例GBD/GBC结算, @see creat_sbd)
         if( percent_gbd <= GAMEBANK_GBD_START_PERCENT )
            dgp.gbd_print_rate = GAMEBANK_100_PERCENT;
         else if( percent_gbd >= GAMEBANK_GBD_STOP_PERCENT )
            dgp.gbd_print_rate = 0;
         else
            dgp.gbd_print_rate = ( ( GAMEBANK_GBD_STOP_PERCENT - percent_gbd ) * GAMEBANK_100_PERCENT ) / ( GAMEBANK_GBD_STOP_PERCENT - GAMEBANK_GBD_START_PERCENT );
      }
   });
} FC_CAPTURE_AND_RETHROW() }

//called by _apply_block
//signing_witness:区块生产者
//new_block: 新生产的区块
void database::update_signing_witness(const witness_object& signing_witness, const signed_block& new_block)
{ try {
   if(signing_witness.last_confirmed_block_num > new_block.block_num() )
      return;

   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   uint64_t new_block_aslot = dpo.current_aslot + get_slot_at_time( new_block.timestamp );

   //更新db中的witness_object
   modify( signing_witness, [&]( witness_object& _wit )
   {
      _wit.last_aslot = new_block_aslot;						//这个见证人上次生成区块对应的时间槽
      _wit.last_confirmed_block_num = new_block.block_num();	//这个见证人上次生成的区块编号
   } );

} FC_CAPTURE_AND_RETHROW() }

void database::update_confirm_witness(const witness_object& confirm_witness, const signed_block& new_block)
{ try {
   if( confirm_witness.last_confirmed_block_num > new_block.block_num() )
      return;

   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   uint64_t new_block_aslot = dpo.current_aslot + get_slot_at_time( new_block.timestamp );

   modify( confirm_witness, [&]( witness_object& _wit )
   {
      _wit.last_aslot = new_block_aslot;
      _wit.last_confirmed_block_num = new_block.block_num();
   } );

   } FC_CAPTURE_AND_RETHROW() 

   update_last_irreversible_block();
}

//持久化
//called by _apply_block
void database::update_last_irreversible_block()
{ try {
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   auto old_last_irreversible = dpo.last_irreversible_block_num;

   /**
    * Prior to voting taking over, we must be more conservative...
    * 每一轮21个见证人最多生成21个区块，这里保守的确定将上一轮生成的最后一个区块做持久化
    * 而不是立即持久化最新生成的区块
    */
   if( head_block_num() < GAMEBANK_START_MINER_VOTING_BLOCK )
   {
   //生产区块不足1个月
      modify( dpo, [&]( dynamic_global_property_object& _dpo )
      {
         if ( head_block_num() > GAMEBANK_MAX_WITNESSES )
			//每一轮21个见证人最多生成21个区块，这里保守的确定将上一轮生成的最后一个区块做持久化
            _dpo.last_irreversible_block_num = head_block_num() - GAMEBANK_MAX_WITNESSES;
      } );
   }
   else
   {
     //生产区块超过1个月
      const witness_schedule_object& wso = get_witness_schedule_object();

      vector< const witness_object* > wit_objs;
      wit_objs.reserve( wso.num_scheduled_witnesses );
      for( int i = 0; i < wso.num_scheduled_witnesses; i++ )
         wit_objs.push_back( &get_witness( wso.current_shuffled_witnesses[i] ) );

		//不可逆阈值
      static_assert( GAMEBANK_IRREVERSIBLE_THRESHOLD > 0, "irreversible threshold must be nonzero" );

      // 1 1 1 2 2 2 2 2 2 2 -> 2     .7*10 = 7
      // 1 1 1 1 1 1 1 2 2 2 -> 1
      // 3 3 3 3 3 3 3 3 3 3 -> 3

		//4分之一 size
      size_t offset = ((GAMEBANK_100_PERCENT - GAMEBANK_IRREVERSIBLE_THRESHOLD) * wit_objs.size() / GAMEBANK_100_PERCENT);

	  // [begin,offset) < offset < (offset,end)//offset的左边所有见证人的最新生产的区块编号均小于offset右边所有见证人最新生产的区块编号
      std::nth_element( wit_objs.begin(), wit_objs.begin() + offset, wit_objs.end(),
         []( const witness_object* a, const witness_object* b )
         {
            return a->last_confirmed_block_num < b->last_confirmed_block_num;
         } );
      // 75%的见证人确认过的区块高度
      uint32_t new_last_irreversible_block_num = wit_objs[offset]->last_confirmed_block_num;

	  //更新全局属性last_irreversible_block_num
      if( new_last_irreversible_block_num > dpo.last_irreversible_block_num )
      {
         modify( dpo, [&]( dynamic_global_property_object& _dpo )
         {
            _dpo.last_irreversible_block_num = new_last_irreversible_block_num;
         } );
      }
   }

   // generic_index中低于该区块高度的记录清除
   commit( dpo.last_irreversible_block_num );

   for( uint32_t i = old_last_irreversible; i <= dpo.last_irreversible_block_num; ++i )
   {
      notify_irreversible_block( i );
   }

   //写入_block_log
   if( !( get_node_properties().skip_flags & skip_block_log ) )
   {
      // output to block log based on new last irreverisible block num
      //写入block log
      const auto& tmp_head = _block_log.head();
      uint64_t log_head_num = 0;

      if( tmp_head )
         log_head_num = tmp_head->block_num();

	  //一次性持久化多个区块
      //持久化的区块:编号为blocklog中最新head的后一个到last_irreversible_block_num这个范围的区块
      if( log_head_num < dpo.last_irreversible_block_num )
      {
         while( log_head_num < dpo.last_irreversible_block_num )
         {
            //从forkdb中取将要持久化的区块写入block log
            shared_ptr< fork_item > block = _fork_db.fetch_block_on_main_branch_by_number( log_head_num+1 );
            FC_ASSERT( block, "Current fork in the fork database does not contain the last_irreversible_block" );
            _block_log.append( block->data );
            log_head_num++;
         }

         _block_log.flush(); // 区块数据持久化
      }
   }

   _fork_db.set_max_size( dpo.head_block_number - dpo.last_irreversible_block_num + 1 );
} FC_CAPTURE_AND_RETHROW() }


bool database::apply_order( const limit_order_object& new_order_object )
{
   auto order_id = new_order_object.id;

   const auto& limit_price_idx = get_index<limit_order_index>().indices().get<by_price>();

   //price{p.quote,p.base}
   auto max_price = ~new_order_object.sell_price;
   auto limit_itr = limit_price_idx.lower_bound(max_price.max());
   auto limit_end = limit_price_idx.upper_bound(max_price);

   bool finished = false;
   while( !finished && limit_itr != limit_end )
   {
      auto old_limit_itr = limit_itr;
      ++limit_itr;
      // match returns 2 when only the old order was fully filled. In this case, we keep matching; otherwise, we stop.
      finished = ( match(new_order_object, *old_limit_itr, old_limit_itr->sell_price) & 0x1 );
   }

   return find< limit_order_object >( order_id ) == nullptr;
}

int database::match( const limit_order_object& new_order, const limit_order_object& old_order, const price& match_price )
{
   bool has_hf_20__1815 = has_hardfork( GAMEBANK_HARDFORK_0_1 );

#pragma message( "TODO:  Remove if(), do assert unconditionally after HF20 occurs" )
   if( has_hf_20__1815 )
   {
      GAMEBANK_ASSERT( new_order.sell_price.quote.symbol == old_order.sell_price.base.symbol,
         order_match_exception, "error matching orders: ${new_order} ${old_order} ${match_price}",
         ("new_order", new_order)("old_order", old_order)("match_price", match_price) );
      GAMEBANK_ASSERT( new_order.sell_price.base.symbol  == old_order.sell_price.quote.symbol,
         order_match_exception, "error matching orders: ${new_order} ${old_order} ${match_price}",
         ("new_order", new_order)("old_order", old_order)("match_price", match_price) );
      GAMEBANK_ASSERT( new_order.for_sale > 0 && old_order.for_sale > 0,
         order_match_exception, "error matching orders: ${new_order} ${old_order} ${match_price}",
         ("new_order", new_order)("old_order", old_order)("match_price", match_price) );
      GAMEBANK_ASSERT( match_price.quote.symbol == new_order.sell_price.base.symbol,
         order_match_exception, "error matching orders: ${new_order} ${old_order} ${match_price}",
         ("new_order", new_order)("old_order", old_order)("match_price", match_price) );
      GAMEBANK_ASSERT( match_price.base.symbol == old_order.sell_price.base.symbol,
         order_match_exception, "error matching orders: ${new_order} ${old_order} ${match_price}",
         ("new_order", new_order)("old_order", old_order)("match_price", match_price) );
   }

   auto new_order_for_sale = new_order.amount_for_sale();
   auto old_order_for_sale = old_order.amount_for_sale();

   asset new_order_pays, new_order_receives, old_order_pays, old_order_receives;

   if( new_order_for_sale <= old_order_for_sale * match_price )
   {
      old_order_receives = new_order_for_sale;
      new_order_receives  = new_order_for_sale * match_price;
   }
   else
   {
      //This line once read: assert( old_order_for_sale < new_order_for_sale * match_price );
      //This assert is not always true -- see trade_amount_equals_zero in operation_tests.cpp
      //Although new_order_for_sale is greater than old_order_for_sale * match_price, old_order_for_sale == new_order_for_sale * match_price
      //Removing the assert seems to be safe -- apparently no asset is created or destroyed.
      new_order_receives = old_order_for_sale;
      old_order_receives = old_order_for_sale * match_price;
   }

   old_order_pays = new_order_receives;
   new_order_pays = old_order_receives;

#pragma message( "TODO:  Remove if(), do assert unconditionally after HF20 occurs" )
   if( has_hf_20__1815 )
   {
      GAMEBANK_ASSERT( new_order_pays == new_order.amount_for_sale() ||
                    old_order_pays == old_order.amount_for_sale(),
         order_match_exception, "error matching orders: ${new_order} ${old_order} ${match_price}",
         ("new_order", new_order)("old_order", old_order)("match_price", match_price) );
   }

  // auto age = head_block_time() - old_order.created;

   push_virtual_operation( fill_order_operation( new_order.seller, new_order.orderid, new_order_pays, old_order.seller, old_order.orderid, old_order_pays ) );

   int result = 0;
   result |= fill_order( new_order, new_order_pays, new_order_receives );
   result |= fill_order( old_order, old_order_pays, old_order_receives ) << 1;

#pragma message( "TODO:  Remove if(), do assert unconditionally after HF20 occurs" )
   if( has_hf_20__1815 )
   {
      GAMEBANK_ASSERT( result != 0,
         order_match_exception, "error matching orders: ${new_order} ${old_order} ${match_price}",
         ("new_order", new_order)("old_order", old_order)("match_price", match_price) );
   }
   return result;
}


void database::adjust_liquidity_reward( const account_object& owner, const asset& volume, bool is_sdb )
{
   const auto& ridx = get_index< liquidity_reward_balance_index >().indices().get< by_owner >();
   auto itr = ridx.find( owner.id );
   if( itr != ridx.end() )
   {
      modify<liquidity_reward_balance_object>( *itr, [&]( liquidity_reward_balance_object& r )
      {
         if( head_block_time() - r.last_update >= GAMEBANK_LIQUIDITY_TIMEOUT_SEC )
         {
            r.gbd_volume = 0;
            r.gbc_volume = 0;
            r.weight = 0;
         }

         if( is_sdb )
            r.gbd_volume += volume.amount.value;
         else
            r.gbc_volume += volume.amount.value;

         r.update_weight( true );
         r.last_update = head_block_time();
      } );
   }
   else
   {
      create<liquidity_reward_balance_object>( [&](liquidity_reward_balance_object& r )
      {
         r.owner = owner.id;
         if( is_sdb )
            r.gbd_volume = volume.amount.value;
         else
            r.gbc_volume = volume.amount.value;

         r.update_weight( true );
         r.last_update = head_block_time();
      } );
   }
}


bool database::fill_order( const limit_order_object& order, const asset& pays, const asset& receives )
{
   try
   {
      GAMEBANK_ASSERT( order.amount_for_sale().symbol == pays.symbol,
         order_fill_exception, "error filling orders: ${order} ${pays} ${receives}",
         ("order", order)("pays", pays)("receives", receives) );
      GAMEBANK_ASSERT( pays.symbol != receives.symbol,
         order_fill_exception, "error filling orders: ${order} ${pays} ${receives}",
         ("order", order)("pays", pays)("receives", receives) );

      adjust_balance( order.seller, receives );

      if( pays == order.amount_for_sale() )
      {
         remove( order );
         return true;
      }
      else
      {
#pragma message( "TODO:  Remove if(), do assert unconditionally after HF20 occurs" )
         if( has_hardfork( GAMEBANK_HARDFORK_0_1 ) )
         {
            GAMEBANK_ASSERT( pays < order.amount_for_sale(),
              order_fill_exception, "error filling orders: ${order} ${pays} ${receives}",
              ("order", order)("pays", pays)("receives", receives) );
         }

         modify( order, [&]( limit_order_object& b )
         {
            b.for_sale -= pays.amount;
         } );
         /**
          *  There are times when the AMOUNT_FOR_SALE * SALE_PRICE == 0 which means that we
          *  have hit the limit where the seller is asking for nothing in return.  When this
          *  happens we must refund any balance back to the seller, it is too small to be
          *  sold at the sale price.
          */
         if( order.amount_to_receive().amount == 0 )
         {
            cancel_order(order);
            return true;
         }
         return false;
      }
   }
   FC_CAPTURE_AND_RETHROW( (order)(pays)(receives) )
}

void database::cancel_order( const limit_order_object& order )
{
   adjust_balance( order.seller, order.amount_for_sale() );
   remove(order);
}


void database::clear_expired_transactions()
{
   //Look for expired transactions in the deduplication list, and remove them.
   //Transactions must have expired by at least two forking windows in order to be removed.
   auto& transaction_idx = get_index< transaction_index >();
   const auto& dedupe_index = transaction_idx.indices().get< by_expiration >();
   while( ( !dedupe_index.empty() ) && ( head_block_time() > dedupe_index.begin()->expiration ) )
      remove( *dedupe_index.begin() );
}

void database::clear_expired_orders()
{
   auto now = head_block_time();
   const auto& orders_by_exp = get_index<limit_order_index>().indices().get<by_expiration>();
   auto itr = orders_by_exp.begin();
   while( itr != orders_by_exp.end() && itr->expiration < now )
   {
      cancel_order( *itr );
      itr = orders_by_exp.begin();
   }
}

void database::clear_expired_nonfungible_funds_on_sale()
{
   auto now = head_block_time();
   const auto& funds_on_sale_by_exp = get_index<nonfungible_fund_on_sale_index>().indices().get<by_expiration>();
   auto itr = funds_on_sale_by_exp.begin();
   while( itr != funds_on_sale_by_exp.end() && itr->expiration < now )
   {
      remove( *itr );
      itr = funds_on_sale_by_exp.begin();
   }
}

void database::clear_expired_delegations()
{
   auto now = head_block_time();
   const auto& delegations_by_exp = get_index< vesting_delegation_expiration_index, by_expiration >();
   auto itr = delegations_by_exp.begin();
   while( itr != delegations_by_exp.end() && itr->expiration < now )
   {
      modify( get_account( itr->delegator ), [&]( account_object& a )
      {
         a.delegated_vesting_shares -= itr->vesting_shares;
      });

      push_virtual_operation( return_vesting_delegation_operation( itr->delegator, itr->vesting_shares ) );

      remove( *itr );
      itr = delegations_by_exp.begin();
   }
}

void database::reclaim_account_creation_delegations()
{
	auto now = head_block_time();
	const auto& didx = get_index< vesting_delegation_index>().indices().get< by_delegation_time >();
	auto itr = didx.lower_bound(GAMEBANK_INIT_MINER_NAME);
	while (itr != didx.end() && itr->delegator == GAMEBANK_INIT_MINER_NAME && itr->min_delegation_time < now)
	{
			modify(get_account(itr->delegatee), [&](account_object& a)
			{
				a.received_vesting_shares -= itr->vesting_shares;
			});
			create< vesting_delegation_expiration_object >([&](vesting_delegation_expiration_object& obj)
			{
				obj.delegator = itr->delegator;
				obj.vesting_shares = itr->vesting_shares;
				obj.expiration = std::max(now + get_dynamic_global_properties().delegation_return_period, itr->min_delegation_time);
			});
			const auto &current = *itr;
			remove(current);
			itr++;
	}	
}

//修改账户存款
//GBC：+= delta
//GBS:  += delta
//GBD: 利息计算/支付 与 += delta
void database::modify_balance( const account_object& a, const asset& delta, bool check_balance )
{
   modify( a, [&]( account_object& acnt )
   {
   //根据货币类型不同，分别处理GBC/GBD/GBS
      switch( delta.symbol.asset_num )
      {
         case GAMEBANK_ASSET_NUM_GBC:
            acnt.balance += delta;
            if( check_balance )
            {
               FC_ASSERT( acnt.balance.amount.value >= 0, "Insufficient GBC funds" );
            }
            break;
         case GAMEBANK_ASSET_NUM_GBD:
			//只有GBD需要进行利息计算
		 	//首先处理GBD利息，再处理余额的变动delta
            if( a.gbd_seconds_last_update != head_block_time() )
            {
				//GBD总量 * 自上次支付利息至今的持有时间
               acnt.gbd_seconds += fc::uint128_t(a.gbd_balance.amount.value) * (head_block_time() - a.gbd_seconds_last_update).to_seconds();
               acnt.gbd_seconds_last_update = head_block_time();

               if( acnt.gbd_seconds > 0 &&
                   (acnt.gbd_seconds_last_update - acnt.gbd_last_interest_payment).to_seconds() > GAMEBANK_GBD_INTEREST_COMPOUND_INTERVAL_SEC )
               {
				//支付GBD持有者利息每月一次
               //利息计算：
               //interest = interest_rate * sbd_seconds / seconds_per_year
                  auto interest = acnt.gbd_seconds / GAMEBANK_SECONDS_PER_YEAR;
                  interest *= get_dynamic_global_properties().gbd_interest_rate;
                  interest /= GAMEBANK_100_PERCENT;
 					//支付的利息（计入sbd_balance）
                  asset interest_paid(interest.to_uint64(), GBD_SYMBOL);
                  acnt.gbd_balance += interest_paid;
                  acnt.gbd_seconds = 0;
                  acnt.gbd_last_interest_payment = head_block_time();

                  if(interest > 0)
                     push_virtual_operation( interest_operation( a.name, interest_paid ) );

                  modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& props)
                  {
					//GBD利息计入GBD发行总量
                     props.current_gbd_supply += interest_paid;
					//折算为GBC的发行总量
                     props.virtual_supply += interest_paid * get_feed_history().current_median_history;
                  } );
               }
            }
			//更改GBD存款余额
            acnt.gbd_balance += delta;
            if( check_balance )
            {
               FC_ASSERT( acnt.gbd_balance.amount.value >= 0, "Insufficient GBD funds" );
            }
            break;
         case GAMEBANK_ASSET_NUM_GBS:
            acnt.vesting_shares += delta;
            if( check_balance )
            {
               FC_ASSERT( acnt.vesting_shares.amount.value >= 0, "Insufficient GBS funds" );
            }
            break;
         default:
            FC_ASSERT( false, "invalid symbol" );
      }
   } );
}

void database::modify_reward_balance( const account_object& a, const asset& value_delta, const asset& share_delta, bool check_balance )
{
   modify( a, [&]( account_object& acnt )
   {
      switch( value_delta.symbol.asset_num )
      {
         case GAMEBANK_ASSET_NUM_GBC:
            if( share_delta.amount.value == 0 )
            {
               acnt.reward_gbc_balance += value_delta;
               if( check_balance )
               {
                  FC_ASSERT( acnt.reward_gbc_balance.amount.value >= 0, "Insufficient reward GBC funds" );
               }
            }
            else
            {
               acnt.reward_vesting_gbc += value_delta;
               acnt.reward_vesting_balance += share_delta;
               if( check_balance )
               {
                  FC_ASSERT( acnt.reward_vesting_balance.amount.value >= 0, "Insufficient reward GBS funds" );
               }
            }
            break;
         case GAMEBANK_ASSET_NUM_GBD:
            FC_ASSERT( share_delta.amount.value == 0 );
            acnt.reward_gbd_balance += value_delta;
            if( check_balance )
            {
               FC_ASSERT( acnt.reward_gbd_balance.amount.value >= 0, "Insufficient reward GBD funds" );
            }
            break;
         default:
            FC_ASSERT( false, "invalid symbol" );
      }
   });
}

//call modify_balance
void database::adjust_balance( const account_object& a, const asset& delta )
{
   bool check_balance = has_hardfork( GAMEBANK_HARDFORK_0_1 );

   modify_balance( a, delta, check_balance );
}

void database::adjust_balance( const account_name_type& name, const asset& delta )
{
   bool check_balance = has_hardfork( GAMEBANK_HARDFORK_0_1 );

   const auto& a = get_account( name );
   modify_balance( a, delta, check_balance );
}

void database::adjust_savings_balance( const account_object& a, const asset& delta )
{
   bool check_balance = has_hardfork( GAMEBANK_HARDFORK_0_1 );

   modify( a, [&]( account_object& acnt )
   {
      switch( delta.symbol.asset_num )
      {
         case GAMEBANK_ASSET_NUM_GBC:
            acnt.savings_balance += delta;
            if( check_balance )
            {
               FC_ASSERT( acnt.savings_balance.amount.value >= 0, "Insufficient savings GBC funds" );
            }
            break;
         case GAMEBANK_ASSET_NUM_GBD:
            if( a.savings_gbd_seconds_last_update != head_block_time() )
            {
               acnt.savings_gbd_seconds += fc::uint128_t(a.savings_gbd_balance.amount.value) * (head_block_time() - a.savings_gbd_seconds_last_update).to_seconds();
               acnt.savings_gbd_seconds_last_update = head_block_time();

               if( acnt.savings_gbd_seconds > 0 &&
                   (acnt.savings_gbd_seconds_last_update - acnt.savings_gbd_last_interest_payment).to_seconds() > GAMEBANK_GBD_INTEREST_COMPOUND_INTERVAL_SEC )
               {
                  auto interest = acnt.savings_gbd_seconds / GAMEBANK_SECONDS_PER_YEAR;
                  interest *= get_dynamic_global_properties().gbd_interest_rate;
                  interest /= GAMEBANK_100_PERCENT;
                  asset interest_paid(interest.to_uint64(), GBD_SYMBOL);
                  acnt.savings_gbd_balance += interest_paid;
                  acnt.savings_gbd_seconds = 0;
                  acnt.savings_gbd_last_interest_payment = head_block_time();

                  if(interest > 0)
                     push_virtual_operation( interest_operation( a.name, interest_paid ) );

                  modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& props)
                  {
                     props.current_gbd_supply += interest_paid;
                     props.virtual_supply += interest_paid * get_feed_history().current_median_history;
                  } );
               }
            }
            acnt.savings_gbd_balance += delta;
            if( check_balance )
            {
               FC_ASSERT( acnt.savings_gbd_balance.amount.value >= 0, "Insufficient savings GBD funds" );
            }
            break;
         default:
            FC_ASSERT( !"invalid symbol" );
      }
   } );
}

void database::adjust_reward_balance( const account_object& a, const asset& value_delta,
                                      const asset& share_delta /*= asset(0,GBS_SYMBOL)*/ )
{
   bool check_balance = has_hardfork( GAMEBANK_HARDFORK_0_1 );
   FC_ASSERT( value_delta.symbol.is_vesting() == false && share_delta.symbol.is_vesting() );

   modify_reward_balance(a, value_delta, share_delta, check_balance);
}

void database::adjust_reward_balance( const account_name_type& name, const asset& value_delta,
                                      const asset& share_delta /*= asset(0,GBS_SYMBOL)*/ )
{
   bool check_balance = has_hardfork( GAMEBANK_HARDFORK_0_1 );
   FC_ASSERT( value_delta.symbol.is_vesting() == false && share_delta.symbol.is_vesting() );

   const auto& a = get_account( name );
   modify_reward_balance(a, value_delta, share_delta, check_balance);
}

void database::adjust_supply( const asset& delta, bool adjust_vesting )
{
   bool check_supply = has_hardfork( GAMEBANK_HARDFORK_0_1 );

   const auto& props = get_dynamic_global_properties();
   if( props.head_block_number < GAMEBANK_BLOCKS_PER_DAY*7 )
      adjust_vesting = false;

   modify( props, [&]( dynamic_global_property_object& props )
   {
      switch( delta.symbol.asset_num )
      {
         case GAMEBANK_ASSET_NUM_GBC:
         {
            asset new_vesting( (adjust_vesting && delta.amount > 0) ? delta.amount * 9 : 0, GBC_SYMBOL );
            props.current_supply += delta + new_vesting;
            props.virtual_supply += delta + new_vesting;
            props.total_vesting_fund_gbc += new_vesting;
            if( check_supply )
            {
               FC_ASSERT( props.current_supply.amount.value >= 0 );
            }
            break;
         }
         case GAMEBANK_ASSET_NUM_GBD:
            props.current_gbd_supply += delta;
            props.virtual_supply = props.current_gbd_supply * get_feed_history().current_median_history + props.current_supply;
            if( check_supply )
            {
               FC_ASSERT( props.current_gbd_supply.amount.value >= 0 );
            }
            break;
         default:
            FC_ASSERT( false, "invalid symbol" );
      }
   } );
}


asset database::get_balance( const account_object& a, asset_symbol_type symbol )const
{
   switch( symbol.asset_num )
   {
      case GAMEBANK_ASSET_NUM_GBC:
         return a.balance;
      case GAMEBANK_ASSET_NUM_GBD:
         return a.gbd_balance;
      default:
      {
      FC_ASSERT( false, "invalid symbol" );
      }
   }
}

asset database::get_savings_balance( const account_object& a, asset_symbol_type symbol )const
{
   switch( symbol.asset_num )
   {
      case GAMEBANK_ASSET_NUM_GBC:
         return a.savings_balance;
      case GAMEBANK_ASSET_NUM_GBD:
         return a.savings_gbd_balance;
      default: 
         FC_ASSERT( !"invalid symbol" );
   }
}

void database::init_hardforks()
{
   _hardfork_times[ 0 ] = fc::time_point_sec( GAMEBANK_GENESIS_TIME );
   _hardfork_versions[ 0 ] = hardfork_version( 0, 0 );
   
#ifdef IS_TEST_NET
   FC_ASSERT( GAMEBANK_HARDFORK_0_1 == 1, "Invalid hardfork configuration" );
   _hardfork_times[ GAMEBANK_HARDFORK_0_1 ] = fc::time_point_sec( GAMEBANK_HARDFORK_0_1_TIME );
   _hardfork_versions[ GAMEBANK_HARDFORK_0_1 ] = GAMEBANK_HARDFORK_0_1_VERSION;
#endif


   const auto& hardforks = get_hardfork_property_object();
   FC_ASSERT( hardforks.last_hardfork <= GAMEBANK_NUM_HARDFORKS, "Chain knows of more hardforks than configuration", ("hardforks.last_hardfork",hardforks.last_hardfork)("GAMEBANK_NUM_HARDFORKS",GAMEBANK_NUM_HARDFORKS) );
   FC_ASSERT( _hardfork_versions[ hardforks.last_hardfork ] <= GAMEBANK_BLOCKCHAIN_VERSION, "Blockchain version is older than last applied hardfork" );
   FC_ASSERT( GAMEBANK_BLOCKCHAIN_HARDFORK_VERSION >= GAMEBANK_BLOCKCHAIN_VERSION );
   FC_ASSERT( GAMEBANK_BLOCKCHAIN_HARDFORK_VERSION == _hardfork_versions[ GAMEBANK_NUM_HARDFORKS ] );
}

void database::process_hardforks()
{
   try
   {
      // If there are upcoming hardforks and the next one is later, do nothing
      const auto& hardforks = get_hardfork_property_object();

         while( _hardfork_versions[ hardforks.last_hardfork ] < hardforks.next_hardfork
            && hardforks.next_hardfork_time <= head_block_time() )
         {
            if( hardforks.last_hardfork < GAMEBANK_NUM_HARDFORKS ) {
               apply_hardfork( hardforks.last_hardfork + 1 );
            }
            else
               throw unknown_hardfork_exception();
         }
      
   }
   FC_CAPTURE_AND_RETHROW()
}

bool database::has_hardfork( uint32_t hardfork )const
{
   return get_hardfork_property_object().processed_hardforks.size() > hardfork;
}

uint32_t database::get_hardfork()const
{
   return get_hardfork_property_object().processed_hardforks.size() - 1;
}

void database::set_hardfork( uint32_t hardfork, bool apply_now )
{
   auto const& hardforks = get_hardfork_property_object();

   for( uint32_t i = hardforks.last_hardfork + 1; i <= hardfork && i <= GAMEBANK_NUM_HARDFORKS; i++ )
   {
         modify( hardforks, [&]( hardfork_property_object& hpo )
         {
            hpo.next_hardfork = _hardfork_versions[i];
            hpo.next_hardfork_time = head_block_time();
         } );


      if( apply_now )
         apply_hardfork( i );
   }
}

void database::apply_hardfork( uint32_t hardfork )
{
   if( _log_hardforks )
      elog( "HARDFORK ${hf} at block ${b}", ("hf", hardfork)("b", head_block_num()) );

   switch( hardfork )
   {
#ifdef IS_TEST_NET
      case GAMEBANK_HARDFORK_0_1:
         {
            modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& gpo )
            {
               gpo.delegation_return_period = GAMEBANK_DELEGATION_RETURN_PERIOD_HF01;
            });

            const auto& wso = get_witness_schedule_object();

            for( const auto& witness : wso.current_shuffled_witnesses )
            {
               // Required check when applying hardfork at genesis
               if( witness != account_name_type() )
               {
                  modify( get< witness_object, by_name >( witness ), [&]( witness_object& w )
                  {
                     w.props.account_creation_fee = asset( w.props.account_creation_fee.amount * GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER, GBC_SYMBOL );
                  });
               }
            }

            modify( wso, [&]( witness_schedule_object& wso )
            {
               wso.median_props.account_creation_fee = asset( wso.median_props.account_creation_fee.amount * GAMEBANK_CREATE_ACCOUNT_WITH_GBC_MODIFIER, GBC_SYMBOL );
            });
         }
         break;
#endif
      default:
         break;
   }

   modify( get_hardfork_property_object(), [&]( hardfork_property_object& hfp )
   {
      FC_ASSERT( hardfork == hfp.last_hardfork + 1, "Hardfork being applied out of order", ("hardfork",hardfork)("hfp.last_hardfork",hfp.last_hardfork) );
      FC_ASSERT( hfp.processed_hardforks.size() == hardfork, "Hardfork being applied out of order" );
      hfp.processed_hardforks.push_back( _hardfork_times[ hardfork ] );
      hfp.last_hardfork = hardfork;
      hfp.current_hardfork_version = _hardfork_versions[ hardfork ];
      FC_ASSERT( hfp.processed_hardforks[ hfp.last_hardfork ] == _hardfork_times[ hfp.last_hardfork ], "Hardfork processing failed sanity check..." );
   } );

   push_virtual_operation( hardfork_operation( hardfork ), true );
}

void database::apply_pre_genesis_patches( void )
{
         perform_vesting_share_split( 1000000 );
#ifdef IS_TEST_NET
         {
            custom_operation test_op;
            string op_msg = "Testnet: Hardfork applied";
            test_op.data = vector< char >( op_msg.begin(), op_msg.end() );
            test_op.required_auths.insert( GAMEBANK_INIT_MINER_NAME );
            operation op = test_op;   // we need the operation object to live to the end of this scope
            operation_notification note( op );
            notify_pre_apply_operation( note );
            notify_post_apply_operation( note );
         }
#endif
      
         retally_witness_votes();
    
         retally_witness_votes();
   
         reset_virtual_schedule_time(*this);
      
         retally_witness_vote_counts();
         retally_comment_children();
      
         retally_witness_vote_counts(true);
   
         retally_liquidity_weight();
      
         {

            modify( get< account_authority_object, by_account >( GAMEBANK_MINER_ACCOUNT ), [&]( account_authority_object& auth )
            {
               auth.posting = authority();
               auth.posting.weight_threshold = 1;
            });

            modify( get< account_authority_object, by_account >( GAMEBANK_NULL_ACCOUNT ), [&]( account_authority_object& auth )
            {
               auth.posting = authority();
               auth.posting.weight_threshold = 1;
            });

            modify( get< account_authority_object, by_account >( GAMEBANK_TEMP_ACCOUNT ), [&]( account_authority_object& auth )
            {
               auth.posting = authority();
               auth.posting.weight_threshold = 1;
            });
         }

         
         {
            modify( get_feed_history(), [&]( feed_history_object& fho )
            {
               while( fho.price_history.size() > GAMEBANK_FEED_HISTORY_WINDOW )
                  fho.price_history.pop_front();
            });
         }
     
  
         {
            static_assert(
               GAMEBANK_MAX_VOTED_WITNESSES + GAMEBANK_MAX_MINER_WITNESSES + GAMEBANK_MAX_RUNNER_WITNESSES == GAMEBANK_MAX_WITNESSES,
               "HF17 witness counts must add up to GAMEBANK_MAX_WITNESSES" );

            modify( get_witness_schedule_object(), [&]( witness_schedule_object& wso )
            {
               wso.max_voted_witnesses = GAMEBANK_MAX_VOTED_WITNESSES;
               wso.max_miner_witnesses = GAMEBANK_MAX_MINER_WITNESSES;
               wso.max_runner_witnesses = GAMEBANK_MAX_RUNNER_WITNESSES;
            });

            const auto& gpo = get_dynamic_global_properties();

            auto post_rf = create< reward_fund_object >( [&]( reward_fund_object& rfo )
            {
               rfo.name = GAMEBANK_POST_REWARD_FUND_NAME;
               rfo.last_update = head_block_time();
               rfo.content_constant = GAMEBANK_CONTENT_CONSTANT_HF0;
               rfo.percent_curation_rewards = GAMEBANK_1_PERCENT * 25;
               rfo.percent_content_rewards = GAMEBANK_100_PERCENT;
               rfo.reward_balance = gpo.total_reward_fund_gbc;
#ifndef IS_TEST_NET
               rfo.recent_claims = GAMEBANK_HF_17_RECENT_CLAIMS;
#endif
               rfo.author_reward_curve = curve_id::quadratic;
               rfo.curation_reward_curve = curve_id::quadratic_curation;
            });

            // As a shortcut in payout processing, we use the id as an array index.
            // The IDs must be assigned this way. The assertion is a dummy check to ensure this happens.
            FC_ASSERT( post_rf.id._id == 0 );

            modify( gpo, [&]( dynamic_global_property_object& g )
            {
               g.total_reward_fund_gbc = asset( 0, GBC_SYMBOL );
               g.total_reward_shares2 = 0;
            });

         }
    
    
         {
            modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& gpo )
            {
               gpo.vote_power_reserve_rate = GAMEBANK_REDUCED_VOTE_POWER_RATE;
            });

            modify( get< reward_fund_object, by_name >( GAMEBANK_POST_REWARD_FUND_NAME ), [&]( reward_fund_object &rfo )
            {
#ifndef IS_TEST_NET
               rfo.recent_claims = GAMEBANK_HF_19_RECENT_CLAIMS;
#endif
               rfo.author_reward_curve = curve_id::linear;
               rfo.curation_reward_curve = curve_id::square_root;
            });

            /* Remove all 0 delegation objects */
            vector< const vesting_delegation_object* > to_remove;
            const auto& delegation_idx = get_index< vesting_delegation_index, by_id >();
            auto delegation_itr = delegation_idx.begin();

            while( delegation_itr != delegation_idx.end() )
            {
               if( delegation_itr->vesting_shares.amount == 0 )
                  to_remove.push_back( &(*delegation_itr) );

               ++delegation_itr;
            }

            for( const vesting_delegation_object* delegation_ptr: to_remove )
            {
               remove( *delegation_ptr );
            }
         }


    // we need not push_virtual_operation for every hardfork operation
  // push_virtual_operation( hardfork_operation( hardfork ), true );
}


void database::retally_liquidity_weight() {
   const auto& ridx = get_index< liquidity_reward_balance_index >().indices().get< by_owner >();
   for( const auto& i : ridx ) {
      modify( i, []( liquidity_reward_balance_object& o ){
         o.update_weight(true/*HAS HARDFORK10 if this method is called*/);
      });
   }
}

/**
 * Verifies all supply invariantes check out
 */
void database::validate_invariants()const
{
   try
   {
      const auto& account_idx = get_index<account_index>().indices().get<by_name>();
      asset total_supply = asset( 0, GBC_SYMBOL );
      asset total_gbd = asset( 0, GBD_SYMBOL );
      asset total_vesting = asset( 0, GBS_SYMBOL );
      asset pending_vesting_gbc = asset( 0, GBC_SYMBOL );
      share_type total_vsf_votes = share_type( 0 );

      auto gpo = get_dynamic_global_properties();

      /// verify no witness has too many votes
      const auto& witness_idx = get_index< witness_index >().indices();
      for( auto itr = witness_idx.begin(); itr != witness_idx.end(); ++itr )
         FC_ASSERT( itr->votes <= gpo.total_vesting_shares.amount, "", ("itr",*itr) );

	  //遍历所有账户
      for( auto itr = account_idx.begin(); itr != account_idx.end(); ++itr )
      {
         total_supply += itr->balance;
         total_supply += itr->savings_balance;
         total_supply += itr->reward_gbc_balance;
		//GBD构成: 1sbd_balance/2savings_sbd_balance/3reward_sbd_balance
         total_gbd += itr->gbd_balance;
         total_gbd += itr->savings_gbd_balance;
         total_gbd += itr->reward_gbd_balance;
         total_vesting += itr->vesting_shares;
         total_vesting += itr->reward_vesting_balance;
         pending_vesting_gbc += itr->reward_vesting_gbc;
         total_vsf_votes += ( itr->proxy == GAMEBANK_PROXY_TO_SELF_ACCOUNT ?
                                 itr->witness_vote_weight() :
                                 ( GAMEBANK_MAX_PROXY_RECURSION_DEPTH > 0 ?
                                      itr->proxied_vsf_votes[GAMEBANK_MAX_PROXY_RECURSION_DEPTH - 1] :
                                      itr->vesting_shares.amount ) );
      }

      const auto& convert_request_idx = get_index< convert_request_index >().indices();
      //遍历所有转换请求
      for( auto itr = convert_request_idx.begin(); itr != convert_request_idx.end(); ++itr )
      {
         if( itr->amount.symbol == GBC_SYMBOL )
            total_supply += itr->amount;
         else if( itr->amount.symbol == GBD_SYMBOL )
			//4将要转换为GBC的GBD数量，计入totol gbd
            total_gbd += itr->amount;
         else
            FC_ASSERT( false, "Encountered illegal symbol in convert_request_object" );
      }

      const auto& limit_order_idx = get_index< limit_order_index >().indices();

	  //遍历order
      for( auto itr = limit_order_idx.begin(); itr != limit_order_idx.end(); ++itr )
      {
         if( itr->sell_price.base.symbol == GBC_SYMBOL )
         {
            total_supply += asset( itr->for_sale, GBC_SYMBOL );
         }
         else if ( itr->sell_price.base.symbol == GBD_SYMBOL )
         {
			//5在市场上将要卖出的GBD数量，计入totol gbd
            total_gbd += asset( itr->for_sale, GBD_SYMBOL );
         }
      }

      const auto& escrow_idx = get_index< escrow_index >().indices().get< by_id >();

	  //遍历escrow
      for( auto itr = escrow_idx.begin(); itr != escrow_idx.end(); ++itr )
      {
         total_supply += itr->gbc_balance;
         total_gbd += itr->gbd_balance;
		//6 escrow交易，准备转出的gbd
         if( itr->pending_fee.symbol == GBC_SYMBOL )
            total_supply += itr->pending_fee;
         else if( itr->pending_fee.symbol == GBD_SYMBOL )
				//7 支付给代理人的费用
            total_gbd += itr->pending_fee;
         else
            FC_ASSERT( false, "found escrow pending fee that is not GBD or GBC" );
      }

      const auto& savings_withdraw_idx = get_index< savings_withdraw_index >().indices().get< by_id >();

      for( auto itr = savings_withdraw_idx.begin(); itr != savings_withdraw_idx.end(); ++itr )
      {
         if( itr->amount.symbol == GBC_SYMBOL )
            total_supply += itr->amount;
         else if( itr->amount.symbol == GBD_SYMBOL )
			//8
            total_gbd += itr->amount;
         else
            FC_ASSERT( false, "found savings withdraw that is not GBD or GBC" );
      }

      const auto& reward_idx = get_index< reward_fund_index, by_id >();

      for( auto itr = reward_idx.begin(); itr != reward_idx.end(); ++itr )
      {
         total_supply += itr->reward_balance;
      }

      total_supply += gpo.total_vesting_fund_gbc + gpo.total_reward_fund_gbc + gpo.pending_rewarded_vesting_gbc;

	  //与gpo保存的数据对比
      FC_ASSERT( gpo.current_supply == total_supply, "", ("gpo.current_supply",gpo.current_supply)("total_supply",total_supply) );
      FC_ASSERT( gpo.current_gbd_supply == total_gbd, "", ("gpo.current_gbd_supply",gpo.current_gbd_supply)("total_gbd",total_gbd) );
      FC_ASSERT( gpo.total_vesting_shares + gpo.pending_rewarded_vesting_shares == total_vesting, "", ("gpo.total_vesting_shares",gpo.total_vesting_shares)("total_vesting",total_vesting) );
      FC_ASSERT( gpo.total_vesting_shares.amount == total_vsf_votes, "", ("total_vesting_shares",gpo.total_vesting_shares)("total_vsf_votes",total_vsf_votes) );
      FC_ASSERT( gpo.pending_rewarded_vesting_gbc == pending_vesting_gbc, "", ("pending_rewarded_vesting_gbc",gpo.pending_rewarded_vesting_gbc)("pending_vesting_gbc", pending_vesting_gbc));

      FC_ASSERT( gpo.virtual_supply >= gpo.current_supply );
      if ( !get_feed_history().current_median_history.is_null() )
      {
         FC_ASSERT( gpo.current_gbd_supply * get_feed_history().current_median_history + gpo.current_supply
            == gpo.virtual_supply, "", ("gpo.current_gbd_supply",gpo.current_gbd_supply)("get_feed_history().current_median_history",get_feed_history().current_median_history)("gpo.current_supply",gpo.current_supply)("gpo.virtual_supply",gpo.virtual_supply) );
      }
   }
   FC_CAPTURE_LOG_AND_RETHROW( (head_block_num()) );
}

void database::perform_vesting_share_split( uint32_t magnitude )
{
   try
   {
      modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& d )
      {
         d.total_vesting_shares.amount *= magnitude;
         d.total_reward_shares2 = 0;
      } );

      // Need to update all GBS in accounts and the total GBS in the dgpo
      for( const auto& account : get_index<account_index>().indices() )
      {
         modify( account, [&]( account_object& a )
         {
            a.vesting_shares.amount *= magnitude;
            a.withdrawn             *= magnitude;
            a.to_withdraw           *= magnitude;
            a.vesting_withdraw_rate  = asset( a.to_withdraw / 104, GBS_SYMBOL ); /*104 weeks*/
            if( a.vesting_withdraw_rate.amount == 0 )
               a.vesting_withdraw_rate.amount = 1;

            for( uint32_t i = 0; i < GAMEBANK_MAX_PROXY_RECURSION_DEPTH; ++i )
               a.proxied_vsf_votes[i] *= magnitude;
         } );
      }

      const auto& comments = get_index< comment_index >().indices();
      for( const auto& comment : comments )
      {
         modify( comment, [&]( comment_object& c )
         {
            c.net_rshares       *= magnitude;
            c.abs_rshares       *= magnitude;
            c.vote_rshares      *= magnitude;
         } );
      }

      for( const auto& c : comments )
      {
         if( c.net_rshares.value > 0 )
            adjust_rshares2( c, 0, util::evaluate_reward_curve( c.net_rshares.value ) );
      }

   }
   FC_CAPTURE_AND_RETHROW()
}

void database::retally_comment_children()
{
   const auto& cidx = get_index< comment_index >().indices();

   // Clear children counts
   for( auto itr = cidx.begin(); itr != cidx.end(); ++itr )
   {
      modify( *itr, [&]( comment_object& c )
      {
         c.children = 0;
      });
   }

   for( auto itr = cidx.begin(); itr != cidx.end(); ++itr )
   {
      if( itr->parent_author != GAMEBANK_ROOT_POST_PARENT )
      {
// Low memory nodes only need immediate child count, full nodes track total children
#ifdef IS_LOW_MEM
         modify( get_comment( itr->parent_author, itr->parent_permlink ), [&]( comment_object& c )
         {
            c.children++;
         });
#else
         const comment_object* parent = &get_comment( itr->parent_author, itr->parent_permlink );
         while( parent )
         {
            modify( *parent, [&]( comment_object& c )
            {
               c.children++;
            });

            if( parent->parent_author != GAMEBANK_ROOT_POST_PARENT )
               parent = &get_comment( parent->parent_author, parent->parent_permlink );
            else
               parent = nullptr;
         }
#endif
      }
   }
}

void database::retally_witness_votes()
{
   const auto& witness_idx = get_index< witness_index >().indices();

   // Clear all witness votes
   for( auto itr = witness_idx.begin(); itr != witness_idx.end(); ++itr )
   {
      modify( *itr, [&]( witness_object& w )
      {
         w.votes = 0;
         w.virtual_position = 0;
      } );
   }

   const auto& account_idx = get_index< account_index >().indices();

   // Apply all existing votes by account
   for( auto itr = account_idx.begin(); itr != account_idx.end(); ++itr )
   {
      if( itr->proxy != GAMEBANK_PROXY_TO_SELF_ACCOUNT ) continue;

      const auto& a = *itr;

      const auto& vidx = get_index<witness_vote_index>().indices().get<by_account_witness>();
      auto wit_itr = vidx.lower_bound( boost::make_tuple( a.name, account_name_type() ) );
      while( wit_itr != vidx.end() && wit_itr->account == a.name )
      {
         adjust_witness_vote( get< witness_object, by_name >(wit_itr->witness), a.witness_vote_weight() );
         ++wit_itr;
      }
   }
}

void database::retally_witness_vote_counts( bool force )
{
   const auto& account_idx = get_index< account_index >().indices();

   // Check all existing votes by account
   for( auto itr = account_idx.begin(); itr != account_idx.end(); ++itr )
   {
      const auto& a = *itr;
      uint16_t witnesses_voted_for = 0;
      if( force || (a.proxy != GAMEBANK_PROXY_TO_SELF_ACCOUNT  ) )
      {
        const auto& vidx = get_index< witness_vote_index >().indices().get< by_account_witness >();
        auto wit_itr = vidx.lower_bound( boost::make_tuple( a.name, account_name_type() ) );
        while( wit_itr != vidx.end() && wit_itr->account == a.name )
        {
           ++witnesses_voted_for;
           ++wit_itr;
        }
      }
      if( a.witnesses_voted_for != witnesses_voted_for )
      {
         modify( a, [&]( account_object& account )
         {
            account.witnesses_voted_for = witnesses_voted_for;
         } );
      }
   }
}


} } //gamebank::chain
