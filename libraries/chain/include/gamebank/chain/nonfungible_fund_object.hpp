#pragma once

#include <gamebank/protocol/authority.hpp>
#include <gamebank/protocol/gamebank_operations.hpp>

#include <gamebank/chain/gamebank_object_types.hpp>


namespace gamebank { namespace chain {

   using chainbase::t_vector;
   using chainbase::t_pair;

   // 不可分割的资产对象
   class nonfungible_fund_object : public object < nonfungible_fund_object_type, nonfungible_fund_object >
   {
	   nonfungible_fund_object() = delete;

      public:
         template< typename Constructor, typename Allocator >
		 nonfungible_fund_object( Constructor&& c, allocator< Allocator > a )
            :meta_data( a ), creator_sign( a )
         {
            c( *this );
         }

         id_type           id;

		 account_name_type creator;		 /// 创建者
		 account_name_type owner;        /// 当前拥有者

         shared_string     meta_data;	 /// 资产附加数据
		 shared_string     creator_sign; /// 创建者的签名数据

         time_point_sec    last_update;
         time_point_sec    created;
   };

   /**
    * @ingroup object_index
    */
   struct by_nonfungible_fund_owner;
   typedef multi_index_container<
	   nonfungible_fund_object,
      indexed_by<
         /// CONSENSUS INDICES - used by evaluators
         ordered_unique< tag< by_id >, member< nonfungible_fund_object, nonfungible_fund_id_type, &nonfungible_fund_object::id > >,
		 ordered_non_unique< tag< by_nonfungible_fund_owner >, member< nonfungible_fund_object, account_name_type, &nonfungible_fund_object::owner > >
      >,
      allocator< nonfungible_fund_object >
   > nonfungible_fund_index;


} } // gamebank::chain

FC_REFLECT( gamebank::chain::nonfungible_fund_object,
             (id)(creator)(owner)
             (meta_data)(creator_sign)
             (last_update)(created)
          )

CHAINBASE_SET_INDEX_TYPE( gamebank::chain::nonfungible_fund_object, gamebank::chain::nonfungible_fund_index)
