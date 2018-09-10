#pragma once
#include <gamebank/plugins/block_api/block_api_objects.hpp>

#include <gamebank/protocol/types.hpp>
#include <gamebank/protocol/transaction.hpp>
#include <gamebank/protocol/block_header.hpp>

#include <gamebank/plugins/json_rpc/utility.hpp>

namespace gamebank { namespace plugins { namespace block_api {

/* get_block_header */

struct get_block_header_args
{
   uint32_t block_num;
};

struct get_block_header_return
{
   optional< block_header > header;
};

/* get_block */
struct get_block_args
{
   uint32_t block_num;
};

struct get_block_return
{
   optional< api_signed_block_object > block;
};

/* get_contract */
struct get_contract_args
{
    uint32_t block_num;
};

struct get_contract_return
{
   optional< api_signed_contract_object > contract;
};

} } } // gamebank::block_api

FC_REFLECT( gamebank::plugins::block_api::get_block_header_args,
   (block_num) )

FC_REFLECT( gamebank::plugins::block_api::get_block_header_return,
   (header) )

FC_REFLECT( gamebank::plugins::block_api::get_block_args,
   (block_num) )

FC_REFLECT( gamebank::plugins::block_api::get_block_return,
   (block) )

FC_REFLECT( gamebank::plugins::block_api::get_contract_args,
   (block_num) )

FC_REFLECT( gamebank::plugins::block_api::get_contract_return,
   (contract) )

