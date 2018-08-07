#pragma once

#include <gamebank/protocol/block.hpp>

namespace gamebank { namespace chain {

struct transaction_notification
{
   transaction_notification( const gamebank::protocol::signed_transaction& tx ) : transaction(tx)
   {
      transaction_id = tx.id();
   }

   gamebank::protocol::transaction_id_type          transaction_id;
   const gamebank::protocol::signed_transaction&    transaction;
};

} }
