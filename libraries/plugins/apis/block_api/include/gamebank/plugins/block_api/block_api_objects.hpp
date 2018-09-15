#pragma once
#include <gamebank/chain/account_object.hpp>
#include <gamebank/chain/block_summary_object.hpp>
#include <gamebank/chain/comment_object.hpp>
#include <gamebank/chain/global_property_object.hpp>
#include <gamebank/chain/history_object.hpp>
#include <gamebank/chain/gamebank_objects.hpp>
#include <gamebank/chain/transaction_object.hpp>
#include <gamebank/chain/witness_objects.hpp>
#include <gamebank/chain/database.hpp>

namespace gamebank { namespace plugins { namespace block_api {

using namespace gamebank::chain;

struct api_signed_block_object : public signed_block
{
   api_signed_block_object( const signed_block& block ) : signed_block( block )
   {
      block_id = id();
      signing_key = signee();
      transaction_ids.reserve( transactions.size() );
      for( const signed_transaction& tx : transactions )
         transaction_ids.push_back( tx.id() );
   }
   api_signed_block_object() {}

   block_id_type                 block_id;
   public_key_type               signing_key;
   vector< transaction_id_type > transaction_ids;
};

struct api_signed_contract_object : public signed_contract
{
   api_signed_contract_object(const signed_contract& contract) : signed_contract(contract) 
   {
      for (const contract_transaction& tx : transactions)
         transaction_ids.push_back(tx.transaction_id);
   }
   api_signed_contract_object() {}

   vector< transaction_id_type > transaction_ids;
};

} } } // gamebank::plugins::database_api

FC_REFLECT_DERIVED( gamebank::plugins::block_api::api_signed_block_object, (gamebank::protocol::signed_block),
                     (block_id)
                     (signing_key)
                     (transaction_ids)
                  )

FC_REFLECT_DERIVED( gamebank::plugins::block_api::api_signed_contract_object, (gamebank::protocol::signed_contract),
                     (transaction_ids)
                  )
