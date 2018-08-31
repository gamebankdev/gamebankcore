#pragma once

#include <gamebank/chain/gamebank_object_types.hpp>

namespace gamebank { namespace chain {

struct bandwidth_notification
{
   bandwidth_notification( const account_name_type& n ) : account_name(n) {}

   int64_t remain_bandwidth = -1;
   int64_t update_bandwidth = 0;
   account_name_type account_name;
};

} }
