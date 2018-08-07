#pragma once

#include <gamebank/protocol/authority.hpp>
#include <gamebank/protocol/gamebank_operations.hpp>

#include <gamebank/chain/gamebank_object_types.hpp>
#include <gamebank/chain/comment_object.hpp>

#include <boost/multi_index/composite_key.hpp>


namespace gamebank { namespace chain {

   using protocol::beneficiary_route_type;
   using chainbase::t_vector;
   using chainbase::t_pair;
     
   // 众筹对象
   class crowdfunding_object : public object < crowdfunding_object_type, crowdfunding_object >
   {
       crowdfunding_object() = delete;

      public:
         template< typename Constructor, typename Allocator >
         crowdfunding_object( Constructor&& c, allocator< Allocator > a )
            :permlink( a )
         {
            c( *this );
         }

         id_type           id;

         account_name_type originator;
         shared_string     permlink;

         time_point_sec    created;
         time_point_sec    expire;
         time_point_sec    last_raise;

         asset             total_raise_value = asset(0, GBC_SYMBOL);
         asset             curator_raise_value = asset(0, GBC_SYMBOL);

         int32_t           finish = 0;
   };

   // 众筹内容描述对象
   class crowdfunding_content_object : public object< crowdfunding_content_object_type, crowdfunding_content_object >
   {
       crowdfunding_content_object() = delete;

   public:
       template< typename Constructor, typename Allocator >
       crowdfunding_content_object(Constructor&& c, allocator< Allocator > a) :
           title(a), body(a), json_metadata(a)
       {
           c(*this);
       }

       id_type                 id;

       crowdfunding_id_type    crowdfunding;

       shared_string           title;
       shared_string           body;
       shared_string           json_metadata;
   };

   // 众筹投资对象
   class crowdfunding_invest_object : public object< crowdfunding_invest_object_type, crowdfunding_invest_object >
   {
   public:
       template< typename Constructor, typename Allocator >
       crowdfunding_invest_object(Constructor&& c, allocator< Allocator > a)
       {
           c(*this);
       }

       id_type                id;

       account_name_type      invester;
       crowdfunding_id_type   crowdfunding;
       time_point_sec         expire;

       time_point_sec         last_update;
       asset                  raise;
   };

   struct by_crowdfunding_invester;
   struct by_expire_invester;

   typedef multi_index_container<
       crowdfunding_invest_object,
       indexed_by<
          ordered_unique< tag< by_id >, member< crowdfunding_invest_object, crowdfunding_invest_id_type, &crowdfunding_invest_object::id > >,
          ordered_unique< tag< by_crowdfunding_invester >,
             composite_key< crowdfunding_invest_object,
                member< crowdfunding_invest_object, crowdfunding_id_type, &crowdfunding_invest_object::crowdfunding>,
                member< crowdfunding_invest_object, account_name_type, &crowdfunding_invest_object::invester>
             >
          >,
          ordered_unique< tag< by_expire_invester >,
             composite_key< crowdfunding_invest_object,
                member< crowdfunding_invest_object, time_point_sec, &crowdfunding_invest_object::expire >,
                member< crowdfunding_invest_object, account_name_type, &crowdfunding_invest_object::invester>
             >
          >
       >,
       allocator< crowdfunding_invest_object >
   > crowdfunding_invest_index;
   
   struct by_permlink;
   struct by_expire_originator;

   typedef multi_index_container<
       crowdfunding_object,
       indexed_by<
         /// CONSENSUS INDICES - used by evaluators
          ordered_unique< tag< by_id >, member< crowdfunding_object, crowdfunding_id_type, &crowdfunding_object::id > >,
          ordered_unique< tag< by_permlink >, /// used by consensus to find posts referenced in ops
             composite_key< crowdfunding_object,
                member< crowdfunding_object, account_name_type, &crowdfunding_object::originator >,
                member< crowdfunding_object, shared_string, &crowdfunding_object::permlink >
             >,
             composite_key_compare< std::less< account_name_type >, strcmp_less >
          >,
          ordered_unique< tag< by_expire_originator >,
             composite_key< crowdfunding_object,
               member< crowdfunding_object, time_point_sec, &crowdfunding_object::expire >,
               member< crowdfunding_object, account_name_type, &crowdfunding_object::originator >
             >
          >
       >,
       allocator< crowdfunding_object >
   > crowdfunding_index;

   struct by_crowdfunding;

   typedef multi_index_container<
       crowdfunding_content_object,
       indexed_by<
       ordered_unique< tag< by_id >, member< crowdfunding_content_object, crowdfunding_content_id_type, &crowdfunding_content_object::id > >,
       ordered_unique< tag< by_crowdfunding >, member< crowdfunding_content_object, crowdfunding_id_type, &crowdfunding_content_object::crowdfunding > >
       >,
       allocator< crowdfunding_content_object >
   > crowdfunding_content_index;

} } // gamebank::chain

FC_REFLECT( gamebank::chain::crowdfunding_object,
             (id)(originator)(permlink)
             (created)(expire)(last_raise)
             (total_raise_value)(curator_raise_value)
          )
CHAINBASE_SET_INDEX_TYPE( gamebank::chain::crowdfunding_object, gamebank::chain::crowdfunding_index )

FC_REFLECT( gamebank::chain::crowdfunding_content_object,(id)(crowdfunding)(title)(body)(json_metadata) )
CHAINBASE_SET_INDEX_TYPE( gamebank::chain::crowdfunding_content_object, gamebank::chain::crowdfunding_content_index )

FC_REFLECT( gamebank::chain::crowdfunding_invest_object, (id)(invester)(crowdfunding)(expire)(last_update)(raise) )
CHAINBASE_SET_INDEX_TYPE( gamebank::chain::crowdfunding_invest_object, gamebank::chain::crowdfunding_invest_index)

namespace helpers
{
   using gamebank::chain::shared_string;
   
   template <>
   class index_statistic_provider<gamebank::chain::crowdfunding_index>
   {
   public:
      typedef gamebank::chain::crowdfunding_index IndexType;
      index_statistic_info gather_statistics(const IndexType& index, bool onlyStaticInfo) const
      {
         index_statistic_info info;
         gather_index_static_data(index, &info);

         if(onlyStaticInfo == false)
         {
            for(const auto& o : index)
            {
               info._item_additional_allocation += o.permlink.capacity()*sizeof(shared_string::value_type);
            }
         }

         return info;
      }
   };


   template <>
   class index_statistic_provider<gamebank::chain::crowdfunding_content_index>
   {
   public:
       typedef gamebank::chain::crowdfunding_content_index IndexType;

       index_statistic_info gather_statistics(const IndexType& index, bool onlyStaticInfo) const
       {
           index_statistic_info info;
           gather_index_static_data(index, &info);

           if (onlyStaticInfo == false)
           {
               for (const auto& o : index)
               {
                   info._item_additional_allocation += o.title.capacity() * sizeof(shared_string::value_type);
                   info._item_additional_allocation += o.body.capacity() * sizeof(shared_string::value_type);
                   info._item_additional_allocation += o.json_metadata.capacity() * sizeof(shared_string::value_type);
               }
           }

           return info;
       }
   };

} /// namespace helpers
