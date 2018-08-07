#pragma once

#include <gamebank/protocol/authority.hpp>
#include <gamebank/protocol/gamebank_operations.hpp>

#include <gamebank/chain/gamebank_object_types.hpp>


namespace gamebank { namespace chain {

   class nonfungible_fund_on_sale_object : public object< nonfungible_fund_on_sale_object_type, nonfungible_fund_on_sale_object >
   {
      public:
         template< typename Constructor, typename Allocator >
         nonfungible_fund_on_sale_object( Constructor&& c, allocator< Allocator > a )
         {
            c( *this );
         }

         nonfungible_fund_on_sale_object(){}

         id_type             id;

         int64_t             fund_id;     //nonfungible_fund_object's id in nonfungible_fund_index
         time_point_sec      created;
         time_point_sec      expiration;
         account_name_type   seller;
         asset               selling_price;
   };

   /**
    * @ingroup object_index
    */
   struct by_expiration;
   struct by_account;
   struct by_fund_id;
   typedef multi_index_container<
       nonfungible_fund_on_sale_object,
       indexed_by<
          ordered_unique< tag< by_id >, member< nonfungible_fund_on_sale_object, nonfungible_fund_on_sale_id_type, &nonfungible_fund_on_sale_object::id > >,
          ordered_unique< tag< by_fund_id >, member< nonfungible_fund_on_sale_object, int64_t, &nonfungible_fund_on_sale_object::fund_id > >,
          ordered_non_unique< tag< by_expiration >, member< nonfungible_fund_on_sale_object, time_point_sec, &nonfungible_fund_on_sale_object::expiration > >,
          ordered_unique< tag< by_account >,
             composite_key< nonfungible_fund_on_sale_object,
                member< nonfungible_fund_on_sale_object, account_name_type, &nonfungible_fund_on_sale_object::seller >,
                member< nonfungible_fund_on_sale_object, int64_t, &nonfungible_fund_on_sale_object::fund_id >
             >
          >
       >,
       allocator< nonfungible_fund_on_sale_object >
    > nonfungible_fund_on_sale_index;



} } // gamebank::chain

FC_REFLECT( gamebank::chain::nonfungible_fund_on_sale_object,
             (id)(fund_id)(created)
             (expiration)(seller)
             (selling_price)
          )

CHAINBASE_SET_INDEX_TYPE( gamebank::chain::nonfungible_fund_on_sale_object, gamebank::chain::nonfungible_fund_on_sale_index)
