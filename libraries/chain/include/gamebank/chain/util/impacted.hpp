#pragma once

#include <fc/container/flat.hpp>
#include <gamebank/protocol/operations.hpp>
#include <gamebank/protocol/transaction.hpp>

#include <fc/string.hpp>

namespace gamebank { namespace app {

using namespace fc;

void operation_get_impacted_accounts(
   const gamebank::protocol::operation& op,
   fc::flat_set<protocol::account_name_type>& result );

void transaction_get_impacted_accounts(
   const gamebank::protocol::transaction& tx,
   fc::flat_set<protocol::account_name_type>& result
   );

} } // gamebank::app
