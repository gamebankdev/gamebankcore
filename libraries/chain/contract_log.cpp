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
					 index_stream.seekg(0, std::ios::end);//windows�汾ʱ����ʱ����ļ�ָ����ĩβ��������ļ�ָ��ĩβ(linux��û���ֹ��������)
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
		 //��block log��ȡ��ǰhead
         my->head = read_head();
         my->head_id = my->head->id();

         if( index_size )
         {
            my->check_block_read();
            my->check_index_read();

            ilog( "Index is nonempty" );
            uint64_t block_pos;
			//�ض�λ��ָ��block log���һ��Pos����head block��pos
            my->block_stream.seekg( -sizeof( uint64_t), std::ios::end );
			//��ȡpos
            my->block_stream.read( (char*)&block_pos, sizeof( block_pos ) );

            uint64_t index_pos;
			//�ض�λ��ָ�������ļ������һ��Pos
            my->index_stream.seekg( -sizeof( uint64_t), std::ios::end );
			//��ȡ���Pos
            my->index_stream.read( (char*)&index_pos, sizeof( index_pos ) );

			//ֻ�������ļ����һ��pos��block log�����һ��pos��Ȳ���ȷ!

			//�����ؽ������ļ�
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

		 //���ص�ǰλ��
         uint64_t pos = my->block_stream.tellp();
         FC_ASSERT( static_cast<uint64_t>(my->index_stream.tellp()) == sizeof( uint64_t ) * ( b.block_num() - 1 ),
            "Append to index file occuring at wrong position.",
            ( "position", (uint64_t) my->index_stream.tellp() )( "expected",( b.block_num() - 1 ) * sizeof( uint64_t ) ) );

		 //���л�����
		 auto data = fc::raw::pack_to_vector( b );
		 
         my->block_stream.write( data.data(), data.size() );		//д��������(Block)
         my->block_stream.write( (char*)&pos, sizeof( pos ) );		//д����λ����Ϣ(Pos of Block)
         my->index_stream.write( (char*)&pos, sizeof( pos ) );		//д�����ļ�

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
   
  //���أ� pair<����, �´ζ�ȡλ��> 
   std::pair< signed_contract, uint64_t > contract_log::read_block( uint64_t pos )const
   {
      scoped_lock lock( my->mtx, defer_lock_l);

      if( my->use_locking )
      {
         lock.lock();;
      }

      return read_block_helper( pos );
   }

   //����pos��Ϣ����block log�ж�ȡ��Ӧ������
   std::pair< signed_contract, uint64_t > contract_log::read_block_helper( uint64_t pos )const
   {
      try
      {
         my->check_block_read();

         my->block_stream.seekg( pos );
         std::pair<signed_contract,uint64_t> result;
		 //��block log�����л���������
         fc::raw::unpack( my->block_stream, result.first );
		 //ע������pos + 8����ʾÿ�ζ���������Զ��ı���һ��Ӧ����POS
         result.second = uint64_t(my->block_stream.tellg()) + 8;
         return result;
      }
      FC_LOG_AND_RETHROW()
   }

   //ͨ��block��Ŷ�ȡblock
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

   //ͨ��block��Ŵ������ļ��ж�ȡPOS
   uint64_t contract_log::get_block_pos( uint32_t block_num ) const
   {
      scoped_lock lock( my->mtx, defer_lock_l);

      if( my->use_locking )
      {
         lock.lock();;
      }

      return get_block_pos_helper( block_num );
   }

   //ͨ��block��ŷ���block��Ӧ��position
   uint64_t contract_log::get_block_pos_helper( uint32_t block_num ) const
   {
      try
      {
         my->check_index_read();

         if( !( my->head.valid() && block_num <= protocol::block_header::num_from_id( my->head_id ) && block_num > 0 ) )
            return npos;
		 //�ض�λ������ǰλ��Ϊ����block_num��λ��
         my->index_stream.seekg( sizeof( uint64_t ) * ( block_num - 1 ) );
         uint64_t pos;
		 //������ǰ���λ�ö�ȡblock_num�������ļ��ж�ӦPosֵ.
         my->index_stream.read( (char*)&pos, sizeof( pos ) );
         return pos;
      }
      FC_LOG_AND_RETHROW()
   }

   //��ȡhead block��������ӵ�block��
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
		 //�ض�λ��block log�����µ������position, ��ָ��[position of head block]�ֶ�
         my->block_stream.seekg( -sizeof(pos), std::ios::end );
		 //��block log��ȡ���position
         my->block_stream.read( (char*)&pos, sizeof(pos) );
		 //�������postion��Ӧ��block����head block
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

//�ؽ�index�ļ���index�ļ���ŵ���block log��ÿ��block��λ����Ϣ
//ͨ������block log����ȡÿ��position����д��index�ļ�
//@see block_log.hpp ͷ�ļ�˵��
   void contract_log::construct_index()
   {
      try
      {
         ilog( "Reconstructing Block Log Index..." );
		 //�ر������ļ�����ɾ�����������ļ�
         my->index_stream.close();
         fc::remove_all( my->index_file );
		 //�ؽ������ļ�
         my->index_stream.open( my->index_file.generic_string().c_str(), LOG_WRITE );
         my->index_write = true;

         uint64_t pos = 0;
         uint64_t end_pos;
         my->check_block_read();
         //�ض�λ��block log��β��ǰƫ��8�ֽ�,��[position of head block]
         my->block_stream.seekg( -sizeof( uint64_t), std::ios::end );
		 //��block log��ȡposition of head block,�����¼����������block log�е�position
         my->block_stream.read( (char*)&end_pos, sizeof( end_pos ) );
         signed_contract tmp;

		 //��λ��block log��ʼ��
         my->block_stream.seekg( pos );

		 //����block log
         while( pos < end_pos )
         {
            fc::raw::unpack( my->block_stream, tmp );	//�����л�block log��ȡblock
            my->block_stream.read( (char*)&pos, sizeof( pos ) );	//��ǰ�����ȡposition
            my->index_stream.write( (char*)&pos, sizeof( pos ) );   //�Ѵ�block log�ж�ȡ��ÿһ��positionд�������ļ�
         }
      }
      FC_LOG_AND_RETHROW()
   }

   void contract_log::set_locking( bool use_locking )
   {
      my->use_locking = true;
   }
} } // gamebank::chain
