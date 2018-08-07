
#include <gamebank/protocol/sign_state.hpp>

namespace gamebank { namespace protocol {

//Ȩ�޵Ĺ�Կ����Ƿ�����Ǯ��
bool sign_state::signed_by( const public_key_type& k )
{
   auto itr = provided_signatures.find(k);
   if( itr == provided_signatures.end() )
   {
   //k����provided_signatures
      auto pk = available_keys.find(k);
      if( pk  != available_keys.end() )
         return provided_signatures[k] = true;
      return false;
   }
   //k��provided_signatures
   return itr->second = true;
}

//�жϳ��Ƿ������㹻��ǩ��
bool sign_state::check_authority( string id )
{
   if( approved_by.find(id) != approved_by.end() ) return true;
   return check_authority( get_active(id) );
}

//@pagram: auth--���������Ȩ��
//�ж�Ȩ�����е��˻�Ȩ�غ�Ȩ�����ҵĹ�ԿȨ���Ƿ�ﵽȨ����ֵ
bool sign_state::check_authority( const authority& auth, uint32_t depth )
{
   uint32_t total_weight = 0;

//����authȨ�޵����й�Կ���

   //�����Կ���Ȩ���ܺͳ�����ֵ����check�ɹ�
   for( const auto& k : auth.key_auths )
   {
      if( signed_by( k.first ) )
      {
      //�����k��Ǯ��
         total_weight += k.second;
         if( total_weight >= auth.weight_threshold )
            return true;
      }
   }

//����authȨ�޵������˻���ɣ����˻�ǩ����

   for( const auto& a : auth.account_auths )
   {
      if( approved_by.find(a.first) == approved_by.end() )
      {
         if( depth == max_recursion )
            continue;
         if( check_authority( get_active( a.first ), depth+1 ) )
         {
            approved_by.insert( a.first );
            total_weight += a.second;
            if( total_weight >= auth.weight_threshold )
               return true;
         }
      }
      else
      {
         //�����˻���approved_by
         total_weight += a.second;
         if( total_weight >= auth.weight_threshold )
            return true;
      }
   }
   return total_weight >= auth.weight_threshold;
}

//���sign_by
bool sign_state::remove_unused_signatures()
{
   vector<public_key_type> remove_sigs;
   for( const auto& sig : provided_signatures )
      if( !sig.second ) remove_sigs.push_back( sig.first );

   for( auto& sig : remove_sigs )
      provided_signatures.erase(sig);

   return remove_sigs.size() != 0;
}

sign_state::sign_state(
   const flat_set<public_key_type>& sigs,
   const authority_getter& a,
   const flat_set<public_key_type>& keys
   ) : get_active(a), available_keys(keys)
{
   for( const auto& key : sigs )
      provided_signatures[ key ] = false;
   approved_by.insert( "temp"  );
}

} } // gamebank::protocol
