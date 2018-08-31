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
       vector<contract_transaction>  transactions;

       block_id_type                 id() const;
       uint32_t                      block_num()const { return num_from_id(previous) + 1; }
       static uint32_t num_from_id(const block_id_type& id);
   };
} } // gamebank::protocol

FC_REFLECT_DERIVED( gamebank::protocol::signed_block, (gamebank::protocol::signed_block_header), (transactions) )
FC_REFLECT( gamebank::protocol::signed_contract, (previous),(transactions) )
