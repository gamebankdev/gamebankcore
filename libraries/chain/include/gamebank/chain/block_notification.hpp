#pragma once

#include <gamebank/protocol/block.hpp>

namespace gamebank { namespace chain {

struct block_notification
{
   block_notification( const gamebank::protocol::signed_block& b ) : block(b)
   {
      block_id = b.id();
      block_num = block_header::num_from_id( block_id );
   }

   gamebank::protocol::block_id_type          block_id;
   uint32_t                                   block_num = 0;
   const gamebank::protocol::signed_block&    block;
   flat_map<transaction_id_type, string>      contract_return;
};

} }
