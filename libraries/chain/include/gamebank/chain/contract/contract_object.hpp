#pragma once

#include <gamebank/chain/gamebank_object_types.hpp>
#include <gamebank/protocol/types.hpp>
#include <gamebank/protocol/asset.hpp>

namespace gamebank { namespace chain {

using namespace std;
using namespace gamebank::protocol;

class contract_object : public object < contract_object_type, contract_object >
{
	contract_object() = delete;

public:
	template< typename Constructor, typename Allocator >
	contract_object(Constructor&& c, allocator< Allocator > a)
		:code(a), abi(a)
	{
		c(*this);
	}

	id_type           id;

    account_name_type name;
	account_name_type creator;
	digest_type		  version;
	shared_string     code;			/// contract code
	shared_string     abi;			/// abi data

    asset             balance = asset(0, GBC_SYMBOL);
	time_point_sec    last_update;
	time_point_sec    created;
};

typedef oid< contract_object > contract_object_id_type;

struct by_create_time;
/**
	* @ingroup object_index
	*/
typedef multi_index_container<
	contract_object,
	indexed_by<
	ordered_unique< tag< by_id >, member< contract_object, contract_object_id_type, &contract_object::id > >,
	ordered_unique< tag< by_name >, member< contract_object, account_name_type, &contract_object::name > >,
    ordered_unique< tag< by_create_time >, member< contract_object, time_point_sec, &contract_object::created > >
	>,
	allocator< contract_object >
> contract_object_index;

}} // gamebank::chain

FC_REFLECT( gamebank::chain::contract_object,
             (id)(name)(creator)(version)
             (code)(abi)
             (balance)(last_update)(created)
          )

CHAINBASE_SET_INDEX_TYPE( gamebank::chain::contract_object, gamebank::chain::contract_object_index)
