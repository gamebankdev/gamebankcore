#pragma once
#include <gamebank/protocol/block_header.hpp>
#include <gamebank/protocol/transaction.hpp>

namespace gamebank { namespace protocol {

   struct signed_block : public signed_block_header
   {
      checksum_type calculate_merkle_root()const;
      vector<signed_transaction> transactions;
   };

} } // gamebank::protocol

FC_REFLECT_DERIVED( gamebank::protocol::signed_block, (gamebank::protocol::signed_block_header), (transactions) )
