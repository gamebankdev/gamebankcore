#pragma once
#include <gamebank/protocol/base.hpp>

#include <gamebank/chain/evaluator.hpp>


namespace gamebank { namespace plugins { namespace follow {

using namespace std;
using gamebank::protocol::account_name_type;
using gamebank::protocol::base_operation;

class follow_plugin;

struct follow_operation : base_operation
{
    account_name_type follower;
    account_name_type following;
    set< string >     what; /// blog, mute

    void validate()const;

    void get_required_posting_authorities( flat_set<account_name_type>& a )const { a.insert( follower ); }
};

struct reblog_operation : base_operation
{
   account_name_type account;
   account_name_type author;
   string            permlink;

   void validate()const;

   void get_required_posting_authorities( flat_set<account_name_type>& a )const { a.insert( account ); }
};

typedef fc::static_variant<
         follow_operation,
         reblog_operation
      > follow_plugin_operation;

GAMEBANK_DEFINE_PLUGIN_EVALUATOR( follow_plugin, follow_plugin_operation, follow );
GAMEBANK_DEFINE_PLUGIN_EVALUATOR( follow_plugin, follow_plugin_operation, reblog );

} } } // gamebank::plugins::follow

FC_REFLECT( gamebank::plugins::follow::follow_operation, (follower)(following)(what) )
FC_REFLECT( gamebank::plugins::follow::reblog_operation, (account)(author)(permlink) )

GAMEBANK_DECLARE_OPERATION_TYPE( gamebank::plugins::follow::follow_plugin_operation )

FC_REFLECT_TYPENAME( gamebank::plugins::follow::follow_plugin_operation )
