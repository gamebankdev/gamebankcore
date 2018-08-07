#pragma once
#include <gamebank/plugins/chain/chain_plugin.hpp>
#include <gamebank/plugins/json_rpc/json_rpc_plugin.hpp>

#include <appbase/application.hpp>

namespace gamebank { namespace plugins { namespace database_api {

using namespace appbase;

#define GAMEBANK_DATABASE_API_PLUGIN_NAME "database_api"

class database_api_plugin : public plugin< database_api_plugin >
{
   public:
      database_api_plugin();
      virtual ~database_api_plugin();

      APPBASE_PLUGIN_REQUIRES(
         (gamebank::plugins::json_rpc::json_rpc_plugin)
         (gamebank::plugins::chain::chain_plugin)
      )

      static const std::string& name() { static std::string name = GAMEBANK_DATABASE_API_PLUGIN_NAME; return name; }

      virtual void set_program_options(
         options_description& cli,
         options_description& cfg ) override;
      virtual void plugin_initialize( const variables_map& options ) override;
      virtual void plugin_startup() override;
      virtual void plugin_shutdown() override;

      std::shared_ptr< class database_api > api;
};

} } } // gamebank::plugins::database_api
