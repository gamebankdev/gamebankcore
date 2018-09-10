#pragma once
#include <gamebank/protocol/block_header.hpp>
#include <gamebank/protocol/transaction.hpp>

namespace gamebank { namespace protocol {

   struct signed_block : public signed_block_header
   {
      checksum_type calculate_merkle_root()const;
      vector<signed_transaction> transactions;
   };

   struct signed_contract
   {
       block_id_type                 previous;
       block_id_type                 block_id;
       vector<contract_transaction>  transactions;
       public_key_type               signing_key;

       block_id_type                 id()const { return block_id; }
       uint32_t                      block_num()const;
   };
} } // gamebank::protocol

FC_REFLECT_DERIVED( gamebank::protocol::signed_block, (gamebank::protocol::signed_block_header), (transactions) )
FC_REFLECT( gamebank::protocol::signed_contract, (previous)(block_id)(transactions)(signing_key))
