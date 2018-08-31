#include <gamebank/chain/contract_log.hpp>
#include <fstream>
#include <fc/io/raw.hpp>

#include <boost/thread/mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/lock_options.hpp>

#define LOG_READ  (std::ios::in | std::ios::binary)
#define LOG_WRITE (std::ios::out | std::ios::binary | std::ios::app)

namespace gamebank { namespace chain {

   typedef boost::interprocess::scoped_lock< boost::mutex > scoped_lock;

   boost::interprocess::defer_lock_type defer_lock_l;

   namespace detail {
      class contract_log_impl {
         public:
            optional< signed_contract > head;
            block_id_type            head_id;
            std::fstream             block_stream;
            std::fstream             index_stream;
            fc::path                 block_file;
            fc::path                 index_file;
            bool                     block_write = false;
            bool                     index_write = false;

            bool                     use_locking = true;

            boost::mutex             mtx;

            inline void check_block_read()
            {
               try
               {
                  if( block_write )
                  {
                     block_stream.close();
                     block_stream.open( block_file.generic_string().c_str(), LOG_READ );
                     block_write = false;
                  }
               }
               FC_LOG_AND_RETHROW()
            }

            inline void check_block_write()
            {
               try
               {
                  if( !block_write )
                  {
                     block_stream.close();
                     block_stream.open( block_file.generic_string().c_str(), LOG_WRITE );
                     block_write = true;
                  }
               }
               FC_LOG_AND_RETHROW()
            }

            inline void check_index_read()
            {
               try
               {
                  if( index_write )
                  {
                     index_stream.close();
                     index_stream.open( index_file.generic_string().c_str(), LOG_READ );
                     index_write = false;
                  }
               }
               FC_LOG_AND_RETHROW()
            }

            inline void check_index_write()
            {
               try
               {
                  if( !index_write )
                  {
                     index_stream.close();
                     index_stream.open( index_file.generic_string().c_str(), LOG_WRITE );
#ifdef _WIN32
					 index_stream.seekg(0, std::ios::end);//windows版本时，有时这个文件指向不是末尾，这里把文件指向末尾(linux下没出现过这个问题)
#endif
                     index_write = true;
                  }
               }
               FC_LOG_AND_RETHROW()
            }
      };
   }

   contract_log::contract_log()
   :my( new detail::contract_log_impl() )
   {
      my->block_stream.exceptions( std::fstream::failbit | std::fstream::badbit );
      my->index_stream.exceptions( std::fstream::failbit | std::fstream::badbit );
   }

   contract_log::~contract_log()
   {
      flush();
   }

   void contract_log::open( const fc::path& file )
   {
      if( my->block_stream.is_open() )
         my->block_stream.close();
      if( my->index_stream.is_open() )
         my->index_stream.close();

      my->block_file = file;
      my->index_file = fc::path( file.generic_string() + ".index" );

      my->block_stream.open( my->block_file.generic_string().c_str(), LOG_WRITE );
      my->index_stream.open( my->index_file.generic_string().c_str(), LOG_WRITE );
      my->block_write = true;
      my->index_write = true;

      /* On startup of the block log, there are several states the log file and the index file can be
       * in relation to eachother.
       *
       *                          Block Log
       *                     Exists       Is New
       *                 +------------+------------+
       *          Exists |    Check   |   Delete   |
       *   Index         |    Head    |    Index   |
       *    File         +------------+------------+
       *          Is New |   Replay   |     Do     |
       *                 |    Log     |   Nothing  |
       *                 +------------+------------+
       *
       * Checking the heads of the files has several conditions as well.
       *  - If they are the same, do nothing.
       *  - If the index file head is not in the log file, delete the index and replay.
       *  - If the index file head is in the log, but not up to date, replay from index head.
       */
      auto log_size = fc::file_size( my->block_file );
      auto index_size = fc::file_size( my->index_file );

      if( log_size )
      {
         ilog( "Log is nonempty" );
		 //从block log读取当前head
         my->head = read_head();
         my->head_id = my->head->id();

         if( index_size )
         {
            my->check_block_read();
            my->check_index_read();

            ilog( "Index is nonempty" );
            uint64_t block_pos;
			//重定位到指向block log最后一个Pos，即head block的pos
            my->block_stream.seekg( -sizeof( uint64_t), std::ios::end );
			//读取pos
            my->block_stream.read( (char*)&block_pos, sizeof( block_pos ) );

            uint64_t index_pos;
			//重定位到指向索引文件的最后一个Pos
            my->index_stream.seekg( -sizeof( uint64_t), std::ios::end );
			//读取这个Pos
            my->index_stream.read( (char*)&index_pos, sizeof( index_pos ) );

			//只有索引文件最后一个pos和block log中最后一个pos相等才正确!

			//否则，重建索引文件
            if( block_pos < index_pos )
            {
               ilog( "block_pos < index_pos, close and reopen index_stream" );
               construct_index();
            }
            else if( block_pos > index_pos )
            {
               ilog( "Index is incomplete" );
               construct_index();
            }
         }
         else
         {
            ilog( "Index is empty" );
            construct_index();
         }
      }
      else if( index_size )
      {
         ilog( "Index is nonempty, remove and recreate it" );
         my->index_stream.close();
         fc::remove_all( my->index_file );
         my->index_stream.open( my->index_file.generic_string().c_str(), LOG_WRITE );
         my->index_write = true;
      }
   }

   void contract_log::close()
   {
      my.reset( new detail::contract_log_impl() );
   }

   bool contract_log::is_open()const
   {
      return my->block_stream.is_open();
   }

   uint64_t contract_log::append( const signed_contract& b )
   {
      try
      {
         scoped_lock lock( my->mtx, defer_lock_l);

         if( my->use_locking )
         {
            lock.lock();;
         }

         my->check_block_write();
         my->check_index_write();

		 //返回当前位置
         uint64_t pos = my->block_stream.tellp();
         FC_ASSERT( static_cast<uint64_t>(my->index_stream.tellp()) == sizeof( uint64_t ) * ( b.block_num() - 1 ),
            "Append to index file occuring at wrong position.",
            ( "position", (uint64_t) my->index_stream.tellp() )( "expected",( b.block_num() - 1 ) * sizeof( uint64_t ) ) );

		 //序列化区块
		 auto data = fc::raw::pack_to_vector( b );
		 
         my->block_stream.write( data.data(), data.size() );		//写区块数据(Block)
         my->block_stream.write( (char*)&pos, sizeof( pos ) );		//写区块位置信息(Pos of Block)
         my->index_stream.write( (char*)&pos, sizeof( pos ) );		//写索引文件

		 my->head = b;
         my->head_id = b.id();

         return pos;
      }
      FC_LOG_AND_RETHROW()
   }

   //flush to disk
   void contract_log::flush()
   {
      scoped_lock lock( my->mtx, defer_lock_l);

            if( my->use_locking )
            {
               lock.lock();;
            }

      my->block_stream.flush();
      my->index_stream.flush();
   }
   
  //返回： pair<区块, 下次读取位置> 
   std::pair< signed_contract, uint64_t > contract_log::read_block( uint64_t pos )const
   {
      scoped_lock lock( my->mtx, defer_lock_l);

      if( my->use_locking )
      {
         lock.lock();;
      }

      return read_block_helper( pos );
   }

   //根据pos信息，从block log中读取对应的区块
   std::pair< signed_contract, uint64_t > contract_log::read_block_helper( uint64_t pos )const
   {
      try
      {
         my->check_block_read();

         my->block_stream.seekg( pos );
         std::pair<signed_contract,uint64_t> result;
		 //从block log反序列化读出区块
         fc::raw::unpack( my->block_stream, result.first );
		 //注意这里pos + 8，表示每次读出区块后，自动改变下一次应读的POS
         result.second = uint64_t(my->block_stream.tellg()) + 8;
         return result;
      }
      FC_LOG_AND_RETHROW()
   }

   //通过block编号读取block
   optional< signed_contract > contract_log::read_block_by_num( uint32_t block_num )const
   {
      try
      {
         scoped_lock lock( my->mtx, defer_lock_l);

         if( my->use_locking )
         {
            lock.lock();;
         }

         optional< signed_contract > b;
         uint64_t pos = get_block_pos_helper( block_num );
         if( pos != npos )
         {
            b = read_block_helper( pos ).first;
            FC_ASSERT( b->block_num() == block_num , "Wrong block was read from block log.", ( "returned", b->block_num() )( "expected", block_num ));
         }
         return b;
      }
      FC_LOG_AND_RETHROW()
   }

   //通过block编号从索引文件中读取POS
   uint64_t contract_log::get_block_pos( uint32_t block_num ) const
   {
      scoped_lock lock( my->mtx, defer_lock_l);

      if( my->use_locking )
      {
         lock.lock();;
      }

      return get_block_pos_helper( block_num );
   }

   //通过block序号返回block对应的position
   uint64_t contract_log::get_block_pos_helper( uint32_t block_num ) const
   {
      try
      {
         my->check_index_read();

         if( !( my->head.valid() && block_num <= protocol::block_header::num_from_id( my->head_id ) && block_num > 0 ) )
            return npos;
		 //重定位流，当前位置为保存block_num的位置
         my->index_stream.seekg( sizeof( uint64_t ) * ( block_num - 1 ) );
         uint64_t pos;
		 //从流当前标记位置读取block_num在索引文件中对应Pos值.
         my->index_stream.read( (char*)&pos, sizeof( pos ) );
         return pos;
      }
      FC_LOG_AND_RETHROW()
   }

   //读取head block（最新添加的block）
   signed_contract contract_log::read_head()const
   {
      try
      {
         scoped_lock lock( my->mtx, defer_lock_l);

         if( my->use_locking )
         {
            lock.lock();;
         }

         my->check_block_read();

         uint64_t pos;
		 //重定位到block log的最新的区块的position, 即指向[position of head block]字段
         my->block_stream.seekg( -sizeof(pos), std::ios::end );
		 //从block log读取这个position
         my->block_stream.read( (char*)&pos, sizeof(pos) );
		 //返回这个postion对应的block，即head block
         return read_block_helper( pos ).first;
      }
      FC_LOG_AND_RETHROW()
   }

   const optional< signed_contract >& contract_log::head()const
   {
      scoped_lock lock( my->mtx, defer_lock_l);

      if( my->use_locking )
      {
         lock.lock();;
      }

      return my->head;
   }

//重建index文件，index文件存放的是block log中每个block的位置信息
//通过遍历block log，读取每个position，并写入index文件
//@see block_log.hpp 头文件说明
   void contract_log::construct_index()
   {
      try
      {
         ilog( "Reconstructing Block Log Index..." );
		 //关闭索引文件流，删除所有索引文件
         my->index_stream.close();
         fc::remove_all( my->index_file );
		 //重建索引文件
         my->index_stream.open( my->index_file.generic_string().c_str(), LOG_WRITE );
         my->index_write = true;

         uint64_t pos = 0;
         uint64_t end_pos;
         my->check_block_read();
         //重定位到block log结尾向前偏移8字节,即[position of head block]
         my->block_stream.seekg( -sizeof( uint64_t), std::ios::end );
		 //从block log读取position of head block,即最新加入的区块在block log中的position
         my->block_stream.read( (char*)&end_pos, sizeof( end_pos ) );
         signed_contract tmp;

		 //定位到block log开始处
         my->block_stream.seekg( pos );

		 //遍历block log
         while( pos < end_pos )
         {
            fc::raw::unpack( my->block_stream, tmp );	//反序列化block log读取block
            my->block_stream.read( (char*)&pos, sizeof( pos ) );	//从前往后读取position
            my->index_stream.write( (char*)&pos, sizeof( pos ) );   //把从block log中读取的每一个position写到索引文件
         }
      }
      FC_LOG_AND_RETHROW()
   }

   void contract_log::set_locking( bool use_locking )
   {
      my->use_locking = true;
   }
} } // gamebank::chain
