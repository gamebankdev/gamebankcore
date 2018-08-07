#include <gamebank/chain/fork_database.hpp>

#include <gamebank/chain/database_exceptions.hpp>

namespace gamebank { namespace chain {

fork_database::fork_database()
{
}
void fork_database::reset()
{
   _head.reset();
   _index.clear();
}

//每一次调用pop_block,将head指针往前移一次，用于回滚
void fork_database::pop_block()
{
   FC_ASSERT( _head, "cannot pop an empty fork database" );
   auto prev = _head->prev.lock();
   FC_ASSERT( prev, "popping head block would leave fork DB empty" );
   _head = prev;
}
//往forkDB插入第一个区块
void     fork_database::start_block(signed_block b)
{
   auto item = std::make_shared<fork_item>(std::move(b));
   _index.insert(item);
   _head = item;//_head指向最新加入item
}

/**
 * Pushes the block into the fork database and caches it if it doesn't link
 *
 */
 //实际调用_push_block(item_ptr)
shared_ptr<fork_item>  fork_database::push_block(const signed_block& b)
{
   auto item = std::make_shared<fork_item>(b);
   try {
      _push_block(item);
   }
   catch ( const unlinkable_block_exception& e )
   {
      wlog( "Pushing block to fork database that failed to link: ${id}, ${num}", ("id",b.id())("num",b.block_num()) );
      wlog( "Head: ${num}, ${id}", ("num",_head->data.block_num())("id",_head->data.id()) );
      throw;
      _unlinked_index.insert( item );
   }
   return _head;
}
//往_index插入新item
//fork_item打包了block，新加入的item必须链接到对应的前一个区块的item(通过block的previous字段)
void  fork_database::_push_block(const item_ptr& item)
{
   if( _head ) // make sure the block is within the range that we are caching
   {
	   //判断将要push的区块编号是否符合要求
      FC_ASSERT( item->num > std::max<int64_t>( 0, int64_t(_head->num) - (_max_size) ),
                 "attempting to push a block that is too old",
                 ("item->num",item->num)("head",_head->num)("max_size",_max_size));
   }

   if( _head && item->previous_id() != block_id_type() )//区块的previous有效
   {
	   //元素为fork_item的hash
      auto& index = _index.get<block_id>();
	  //查询当前push的区块的前一个区块是否在_index中保存
      auto itr = index.find(item->previous_id());
      GAMEBANK_ASSERT(itr != index.end(), unlinkable_block_exception, "block does not link to known chain");
      FC_ASSERT(!(*itr)->invalid);
	  //新加的区块指向它的前一个区块（从数据结构来说，item构成链表）
      item->prev = *itr;
   }
   //新的区块加入到_index,上链？
   _index.insert(item);
   //如果不存在head，则head指向当前新区块
   //如果存在head，且新区块编号大于head，则更新head为新区块
   if( !_head || item->num > _head->num ) _head = item;
}

/**
 *  Iterate through the unlinked cache and insert anything that
 *  links to the newly inserted item.  This will start a recursive
 *  set of calls performing a depth-first insertion of pending blocks as
 *  _push_next(..) calls _push_block(...) which will in turn call _push_next
 */
void fork_database::_push_next( const item_ptr& new_item )
{
	//通过前序id索引（得到的是无序且可重复的hash）
    auto& prev_idx = _unlinked_index.get<by_previous>();

	//在prev_idx查找是否有新区块的id
    auto itr = prev_idx.find( new_item->id );
	//如果prev_idx中存在与新区块相同的id，则删除idx保存的item_ptr，并把它添加到_index
    while( itr != prev_idx.end() )
    {
       auto tmp = *itr;
       prev_idx.erase( itr );
	   //从_unlinked_index转移到_index
       _push_block( tmp );
	   //继续查找
       itr = prev_idx.find( new_item->id );
    }
}

//设置_index和_unlinked_index的最大可容纳item_ptr个数
void fork_database::set_max_size( uint32_t s )
{
   _max_size = s;
   if( !_head ) return;

   { /// index
	 //_index只保留区块编号最大的_max_size个
      auto& by_num_idx = _index.get<block_num>();
      auto itr = by_num_idx.begin();
      while( itr != by_num_idx.end() )
      {
         if( (*itr)->num < std::max(int64_t(0),int64_t(_head->num) - _max_size) )
            by_num_idx.erase(itr);
         else
            break;
         itr = by_num_idx.begin();
      }
   }
   { /// unlinked_index
	 //unlinked_index只保留区块编号最大的_max_size个
      auto& by_num_idx = _unlinked_index.get<block_num>();
      auto itr = by_num_idx.begin();
      while( itr != by_num_idx.end() )
      {
         if( (*itr)->num < std::max(int64_t(0),int64_t(_head->num) - _max_size) )
            by_num_idx.erase(itr);
         else
            break;
         itr = by_num_idx.begin();
      }
   }
}

//判断某个区块是否在_index/_unlinked_index任一个中
bool fork_database::is_known_block(const block_id_type& id)const
{
	//首先判断是否在_index
   auto& index = _index.get<block_id>();
   auto itr = index.find(id);
   if( itr != index.end() )
      return true;
   //如果不在_index，则继续判断是否在_unlinked_index
   auto& unlinked_index = _unlinked_index.get<block_id>();
   auto unlinked_itr = unlinked_index.find(id);
   return unlinked_itr != unlinked_index.end();
}

//根据block id从_index或_unlinked_index提取对应的item_ptr
item_ptr fork_database::fetch_block(const block_id_type& id)const
{
	//优先尝试先从_index提取
   auto& index = _index.get<block_id>();
   auto itr = index.find(id);
   if( itr != index.end() )
      return *itr;
   //如果不在_index，则尝试从_unlinked_index提取
   auto& unlinked_index = _unlinked_index.get<block_id>();
   auto unlinked_itr = unlinked_index.find(id);
   if( unlinked_itr != unlinked_index.end() )
      return *unlinked_itr;
   //都不存在，则新建一个item_ptr
   return item_ptr();
}

//根据block Number从_index中提取所有相同编号的item_ptr
//返回所有满足区块编号的item_ptr
vector<item_ptr> fork_database::fetch_block_by_number(uint32_t num)const
{
   try
   {
   vector<item_ptr> result;
   auto itr = _index.get<block_num>().find(num);
   //_index可能存在多个相同区块编号的item_ptr
   while( itr != _index.get<block_num>().end() )
   {
      if( (*itr)->num == num )
         result.push_back( *itr );
      else
         break;
      ++itr;
   }
   return result;
   }
   FC_LOG_AND_RETHROW()
}

//返回值保存的是两个分支各自从尾部到共同祖先前一个节点的所有item
pair<fork_database::branch_type,fork_database::branch_type>
  fork_database::fetch_branch_from(block_id_type first, block_id_type second)const
{ try {
   // This function gets a branch (i.e. vector<fork_item>) leading
   // back to the most recent common ancestor.
   pair<branch_type,branch_type> result;
   auto first_branch_itr = _index.get<block_id>().find(first);
   FC_ASSERT(first_branch_itr != _index.get<block_id>().end());
   auto first_branch = *first_branch_itr;

   auto second_branch_itr = _index.get<block_id>().find(second);
   FC_ASSERT(second_branch_itr != _index.get<block_id>().end());
   auto second_branch = *second_branch_itr;

   //保存两个分支中编号更大的那部分item
   while( first_branch->data.block_num() > second_branch->data.block_num() )
   {
      result.first.push_back(first_branch);
      first_branch = first_branch->prev.lock();
      FC_ASSERT(first_branch);
   }
   while( second_branch->data.block_num() > first_branch->data.block_num() )
   {
      result.second.push_back( second_branch );
      second_branch = second_branch->prev.lock();
      FC_ASSERT(second_branch);
   }
   // 一直回溯到分叉点:这2个区块指向同一个区块
   while( first_branch->data.previous != second_branch->data.previous )
   {
      result.first.push_back(first_branch);
      result.second.push_back(second_branch);
      first_branch = first_branch->prev.lock();
      FC_ASSERT(first_branch);
      second_branch = second_branch->prev.lock();
      FC_ASSERT(second_branch);
   }
   if( first_branch && second_branch )
   {
      result.first.push_back(first_branch);
      result.second.push_back(second_branch);
   }
   return result;
} FC_CAPTURE_AND_RETHROW( (first)(second) ) }

//从head节点向前遍历直到当前节点的区块编号小于block_num
//返回当前满足条件的节点（小于block_num）
shared_ptr<fork_item> fork_database::walk_main_branch_to_num( uint32_t block_num )const
{
	//从区块编号最大的item开始
   shared_ptr<fork_item> next = head();
   if( block_num > next->num )
      return shared_ptr<fork_item>();

   //从最大编号开始遍历，直到block_num大于遍历的节点
   while( next.get() != nullptr && next->num > block_num )
      next = next->prev.lock();//weak_ptr to shared_ptr
	//next当前的指向是小于blockNum的
   return next;
}

shared_ptr<fork_item> fork_database::fetch_block_on_main_branch_by_number( uint32_t block_num )const
{
	//从_index获取所有满足block number的区块
   vector<item_ptr> blocks = fetch_block_by_number(block_num);
   if( blocks.size() == 1 )
      return blocks[0];//只有一个
   if( blocks.size() == 0 )
      return shared_ptr<fork_item>();//没有找到

	//有多个编号相同的区块
	//继续遍历_index
   return walk_main_branch_to_num(block_num);
}

void fork_database::set_head(shared_ptr<fork_item> h)
{
   _head = h;
}

void fork_database::remove(block_id_type id)
{
   _index.get<block_id>().erase(id);
}

} } // gamebank::chain
