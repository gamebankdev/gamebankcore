#include <gamebank/plugins/follow/follow_operations.hpp>

#include <gamebank/protocol/operation_util_impl.hpp>

namespace gamebank { namespace plugins{ namespace follow {

void follow_operation::validate()const
{
   FC_ASSERT( follower != following, "You cannot follow yourself" );
}

void reblog_operation::validate()const
{
   FC_ASSERT( account != author, "You cannot reblog your own content" );
}

} } } //gamebank::plugins::follow

GAMEBANK_DEFINE_OPERATION_TYPE( gamebank::plugins::follow::follow_plugin_operation )
