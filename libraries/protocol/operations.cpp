#include <gamebank/protocol/operations.hpp>

#include <gamebank/protocol/operation_util_impl.hpp>

namespace gamebank { namespace protocol {

struct is_market_op_visitor {
   typedef bool result_type;

   template<typename T>
   bool operator()( T&& v )const { return false; }
   bool operator()( const limit_order_create_operation& )const { return true; }
   bool operator()( const limit_order_cancel_operation& )const { return true; }
   bool operator()( const transfer_operation& )const { return true; }
   bool operator()( const transfer_to_vesting_operation& )const { return true; }
   bool operator()( const contract_deploy_operation& )const { return true; }
};

bool is_market_operation( const operation& op ) {
   return op.visit( is_market_op_visitor() );
}

struct is_vop_visitor
{
   typedef bool result_type;

   template< typename T >
   bool operator()( const T& v )const { return v.is_virtual(); }
};

bool is_virtual_operation( const operation& op )
{
   return op.visit( is_vop_visitor() );
}

struct is_need_bandwidth_visitor
{
	typedef bool result_type;

	template<typename T>
	bool operator()(T&& v)const { return true; }
	bool operator()(const transfer_to_vesting_operation &v)const { return false; }
};

bool is_need_update_bandwidth_operation(const operation& op)
{
	return op.visit(is_need_bandwidth_visitor());
}

} } // gamebank::protocol

GAMEBANK_DEFINE_OPERATION_TYPE( gamebank::protocol::operation )
