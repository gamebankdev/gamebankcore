#pragma once

#include <gamebank/plugins/contract/contract_plugin.hpp>
#include <gamebank/plugins/chain/chain_plugin.hpp>
#include <appbase/application.hpp>

#define GAMEBANK_CONTRACT_PLUGIN_NAME "contract"

namespace gamebank { namespace plugins { namespace contract {

namespace detail { class contract_plugin_impl; }

class contract_plugin : public appbase::plugin< contract_plugin >
{
public:
   APPBASE_PLUGIN_REQUIRES(
      (gamebank::plugins::chain::chain_plugin)
   )

   contract_plugin();
   virtual ~contract_plugin();

   static const std::string& name() { static std::string name = GAMEBANK_CONTRACT_PLUGIN_NAME; return name; }

   virtual void set_program_options(
      boost::program_options::options_description &command_line_options,
      boost::program_options::options_description &config_file_options
      ) override;

   virtual void plugin_initialize(const boost::program_options::variables_map& options) override;
   virtual void plugin_startup() override;
   virtual void plugin_shutdown() override;

private:
   std::unique_ptr< detail::contract_plugin_impl > my;
};

} } } // gamebank::plugins::contract
