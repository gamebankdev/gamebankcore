#include <gamebank/plugins/witness_api/witness_api_plugin.hpp>
#include <gamebank/plugins/witness_api/witness_api.hpp>

namespace gamebank { namespace plugins { namespace witness {

namespace detail {

class witness_api_impl
{
   public:
      witness_api_impl() : _db( appbase::app().get_plugin< gamebank::plugins::chain::chain_plugin >().db() ) {}

      DECLARE_API_IMPL(
         (get_account_bandwidth)
         (get_reserve_ratio)
      )

      chain::database& _db;
};

DEFINE_API_IMPL( witness_api_impl, get_account_bandwidth )
{
   get_account_bandwidth_return result;

   auto band = _db.find< witness::account_bandwidth_object, witness::by_account_bandwidth_type >( boost::make_tuple( args.account, args.type ) );
   if( band != nullptr )
      result.bandwidth = *band;

   return result;
}

DEFINE_API_IMPL( witness_api_impl, get_reserve_ratio )
{
   return _db.get( reserve_ratio_id_type() );
}

} // detail

witness_api::witness_api(): my( new detail::witness_api_impl() )
{
   JSON_RPC_REGISTER_API( GAMEBANK_WITNESS_API_PLUGIN_NAME );
}

witness_api::~witness_api() {}

DEFINE_READ_APIS( witness_api,
   (get_account_bandwidth)
   (get_reserve_ratio)
)

} } } // gamebank::plugins::witness
