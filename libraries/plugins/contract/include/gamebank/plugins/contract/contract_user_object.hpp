#pragma once

#include <boost/multi_index/composite_key.hpp>
#include <gamebank/chain/gamebank_object_types.hpp>


namespace gamebank { namespace plugins { namespace contract {

using namespace std;
using namespace gamebank::chain;

class contract_user_object : public object < contract_user_object_type, contract_user_object >
{
	contract_user_object() = delete;

public:
	template< typename Constructor, typename Allocator >
	contract_user_object(Constructor&& c, allocator< Allocator > a)
		:data(a)
	{
		c(*this);
	}

	id_type           id;

	account_name_type contract_name;
	account_name_type user_name;
	shared_string     data;			/// user save data

	time_point_sec    last_update;
	time_point_sec    created;
};

typedef oid< contract_user_object > contract_user_object_id_type;

struct by_contract_user;
typedef multi_index_container<
	contract_user_object,
	indexed_by<
	ordered_unique< tag< by_id >, member< contract_user_object, contract_user_object_id_type, &contract_user_object::id > >,
	ordered_unique< tag< by_contract_user >,
	composite_key< contract_user_object,
	member< contract_user_object, account_name_type, &contract_user_object::contract_name>,
	member< contract_user_object, account_name_type, &contract_user_object::user_name>
	>
	>
	>,
	allocator< contract_user_object >
> contract_user_object_index;


}}}// gamebank::plugins::contract

FC_REFLECT( gamebank::plugins::contract::contract_user_object,
             (id)(contract_name)(user_name)
             (data)
             (last_update)(created)
          )

CHAINBASE_SET_INDEX_TYPE( gamebank::plugins::contract::contract_user_object, gamebank::plugins::contract::contract_user_object_index)
