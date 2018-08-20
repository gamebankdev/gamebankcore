
#include <gamebank/protocol/transaction.hpp>
#include <gamebank/protocol/transaction_util.hpp>

#include <fc/io/raw.hpp>
#include <fc/bitutil.hpp>
#include <fc/smart_ref_impl.hpp>

#include <algorithm>

namespace gamebank { namespace protocol {

digest_type signed_transaction::merkle_digest()const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   return enc.result();
}

//���л�transaction������ժҪ
digest_type transaction::digest()const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   //���ؾ���SHA265�Ľ��׼�������(ժҪ)
   return enc.result();
}

//���л�chain_id + ���ף���������ID�Ľ���ժҪ
digest_type transaction::sig_digest( const chain_id_type& chain_id )const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, chain_id );
   fc::raw::pack( enc, *this );
   return enc.result();
}

//���������в��������Ϸ���У��
void transaction::validate() const
{
   FC_ASSERT( operations.size() > 0, "A transaction must have at least one operation", ("trx",*this) );
	/*
	��ÿ�ֲ�����֤:�����Ƿ�����Ҫ��
	���ջ���õ�gamebank_operations.cpp��
	@see:	gamebank_operations.cpp �е� XXX_operation::validate()
	*/
   for( const auto& op : operations )
      operation_validate(op);
}

//���ɽ���id
gamebank::protocol::transaction_id_type gamebank::protocol::transaction::id() const
{
  //���ν���ժҪ
   auto h = digest();
   transaction_id_type result;
   //ժҪ��hash --> ���׵�hash == ����id
   memcpy(result._hash, h._hash, std::min(sizeof(result), sizeof(h)));
   return result;
}

//���ɽ���ǩ������������ǩ��������
const signature_type& gamebank::protocol::signed_transaction::sign(const private_key_type& key, const chain_id_type& chain_id)
{
   //���ض�������ID�ͱ��ν��׽������л������ؼ��ܺ��ժҪ
   digest_type h = sig_digest( chain_id );
   //˽Կ����ǩ����ӵ�signatures������
   signatures.push_back(key.sign_compact(h));
   return signatures.back();
}

//���л�����ժҪ������ǩ��(signature_type)
signature_type gamebank::protocol::signed_transaction::sign(const private_key_type& key, const chain_id_type& chain_id)const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, chain_id );
   fc::raw::pack( enc, *this );
   return key.sign_compact(enc.result());
}

void transaction::set_expiration( fc::time_point_sec expiration_time )
{
    expiration = expiration_time;
}

void transaction::set_reference_block( const block_id_type& reference_block )
{
   ref_block_num = fc::endian_reverse_u32(reference_block._hash[0]);
   ref_block_prefix = reference_block._hash[1];
}

void transaction::get_required_authorities( flat_set< account_name_type >& active,	//���������active��Ȩ�˺�
                                            flat_set< account_name_type >& owner,		   //���������owner��Ȩ�˺�
                                            flat_set< account_name_type >& posting,		   //���������posting��Ȩ�˺�
                                            vector< authority >& other )const
{
//�õ���������Ҫ������˻�
   for( const auto& op : operations )
      operation_get_required_authorities( op, active, owner, posting, other );
}

//��ȡ��Կ��������֤����ǩ��
flat_set<public_key_type> signed_transaction::get_signature_keys( const chain_id_type& chain_id )const
{ try {
   auto d = sig_digest( chain_id );
   flat_set<public_key_type> result;
   //��ȡ����������֤����ǩ���Ĺ�Կ
   for( const auto&  sig : signatures )
   {
      GAMEBANK_ASSERT(
		 //ͨ��ǩ���ͽ���ժҪ(digest)��ȡ��Կ
         result.insert( fc::ecc::public_key(sig,d) ).second,
         tx_duplicate_sig,
         "Duplicate Signature detected" );
   }
   return result;
} FC_CAPTURE_AND_RETHROW() }



set<public_key_type> signed_transaction::get_required_signatures(
   const chain_id_type& chain_id,
   const flat_set<public_key_type>& available_keys,
   const authority_getter& get_active,
   const authority_getter& get_owner,
   const authority_getter& get_posting,
   uint32_t max_recursion_depth )const
{
   flat_set< account_name_type > required_active;
   flat_set< account_name_type > required_owner;
   flat_set< account_name_type > required_posting;
   vector< authority > other;

   //���в�����Ҫ����Ȩ�˺�
   get_required_authorities( required_active, required_owner, required_posting, other );

   /** posting authority cannot be mixed with active authority in same transaction */

   //��Post��Ȩ��Ҫ
   if( required_posting.size() ) {
   	  //get_signature_keys( chain_id )�� ��ǩ���Ĺ�Կ
   	  //available_keys:Ǯ�����ڵĹ�Կ
      sign_state s(get_signature_keys( chain_id ),get_posting,available_keys);
      s.max_recursion = max_recursion_depth;

	  //������owner��active��Ȩ��Ҫ����
      FC_ASSERT( !required_owner.size() );
      FC_ASSERT( !required_active.size() );

	  //�ж�ÿһ��post�˻��Ƿ����㹻��key
      for( auto& posting : required_posting )
         s.check_authority( posting  );

	  //����provided_signatures
      s.remove_unused_signatures();

	  //����Ǯ���д��ڵĹ�Կ���
      set<public_key_type> result;

      for( auto& provided_sig : s.provided_signatures )
         if( available_keys.find( provided_sig.first ) != available_keys.end() )
            result.insert( provided_sig.first );

      return result;
   }

//����Ҫpost��Ȩ,ֻ��Ҫowner����active

   //�洢ǩ��״̬
   //get_signature_keys( chain_id )�� ��ǩ���Ĺ�Կ
   //available_keys:Ǯ�����ڵĹ�Կ
   sign_state s(get_signature_keys( chain_id ),get_active,available_keys);
   s.max_recursion = max_recursion_depth;

   for( const auto& auth : other )
      s.check_authority( auth );
   for( auto& owner : required_owner )
      s.check_authority( get_owner( owner ) );
   for( auto& active : required_active )
      s.check_authority( active  );

   s.remove_unused_signatures();

   set<public_key_type> result;

   for( auto& provided_sig : s.provided_signatures )
      if( available_keys.find( provided_sig.first ) != available_keys.end() )
         result.insert( provided_sig.first );

   return result;
}

set<public_key_type> signed_transaction::minimize_required_signatures(
   const chain_id_type& chain_id,							//������id
   const flat_set< public_key_type >& available_keys,		//Ǯ���д��ڵ�Ȩ�޹�Կ���

   //typedef std::function<authority(const string&)> authority_getter;
   const authority_getter& get_active,
   const authority_getter& get_owner,
   const authority_getter& get_posting,
   uint32_t max_recursion
   ) const
{
   //s: Ǯ���д��ڵĹ�Կ��ɡ���Ҫǩ���Ĺ�Կ��
   set< public_key_type > s = get_required_signatures( chain_id, available_keys, get_active, get_owner, get_posting, max_recursion );
   flat_set< public_key_type > result( s.begin(), s.end() );

   for( const public_key_type& k : s )
   {
      result.erase( k );
      try
      {
//do check authority������ɹ�������Ҫǩ��
         gamebank::protocol::verify_authority( operations, result, get_active, get_owner, get_posting, max_recursion );
         continue;  // element stays erased if verify_authority is ok
      }
	  //��֤ʧ��
      catch( const tx_missing_owner_auth& e ) {}
      catch( const tx_missing_active_auth& e ) {}
      catch( const tx_missing_posting_auth& e ) {}
      catch( const tx_missing_other_auth& e ) {}
      result.insert( k );
   }
   //������Ҫǩ���Ĺ�Կ
   return set<public_key_type>( result.begin(), result.end() );
}

//��֤ǩ��(��_apply_transcation�е���)
void signed_transaction::verify_authority(
   const chain_id_type& chain_id,
   const authority_getter& get_active,
   const authority_getter& get_owner,
   const authority_getter& get_posting,
   uint32_t max_recursion )const
{ try {
//�ӽ���ժҪ��ȡ��Կ����֤����ǩ��
   gamebank::protocol::verify_authority( operations, get_signature_keys( chain_id ), get_active, get_owner, get_posting, max_recursion );
} FC_CAPTURE_AND_RETHROW( (*this) ) }

} } // gamebank::protocol
