#include <gamebank/plugins/contract/contract_plugin.hpp>
#include <gamebank/chain/index.hpp>
#include <gamebank/plugins/contract/contract_object.hpp>
#include <gamebank/plugins/contract/contract_user_object.hpp>
#include <iostream>

namespace gamebank { namespace plugins { namespace contract {

using std::string;
using std::vector;

namespace bpo = boost::program_options;

namespace detail {

   class contract_plugin_impl {
   public:
	   contract_plugin_impl() :
         _chain_plugin( appbase::app().get_plugin< gamebank::plugins::chain::chain_plugin >() ),
         _db( appbase::app().get_plugin< gamebank::plugins::chain::chain_plugin >().db() )
         {}

      plugins::chain::chain_plugin& _chain_plugin;
      chain::database&              _db;
   };

} // detail


contract_plugin::contract_plugin() {}
contract_plugin::~contract_plugin() {}

void contract_plugin::set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg)
{
}

void contract_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{ try {
   ilog( "Initializing contract plugin" );
   my = std::make_unique< detail::contract_plugin_impl >();

   add_plugin_index< contract_object_index >( my->_db );
   add_plugin_index< contract_user_object_index >(my->_db);

} FC_LOG_AND_RETHROW() }

void contract_plugin::plugin_startup()
{ try {
   ilog("contract plugin:  plugin_startup() begin" );
   // ...
   ilog("contract plugin:  plugin_startup() end");
   } FC_CAPTURE_AND_RETHROW() }

void contract_plugin::plugin_shutdown()
{
   try
   {
	   // ...
   }
   catch(fc::exception& e)
   {
      edump( (e.to_detail_string()) );
   }
}

} } } // gamebank::plugins::contract
