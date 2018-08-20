
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

//序列化transaction，交易摘要
digest_type transaction::digest()const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, *this );
   //返回经过SHA265的交易加密数据(摘要)
   return enc.result();
}

//序列化chain_id + 交易，含区块链ID的交易摘要
digest_type transaction::sig_digest( const chain_id_type& chain_id )const
{
   digest_type::encoder enc;
   fc::raw::pack( enc, chain_id );
   fc::raw::pack( enc, *this );
   return enc.result();
}

//交易中所有操作参数合法性校验
void transaction::validate() const
{
   FC_ASSERT( operations.size() > 0, "A transaction must have at least one operation", ("trx",*this) );
	/*
	对每种操作验证:参数是否满足要求
	最终会调用到gamebank_operations.cpp中
	@see:	gamebank_operations.cpp 中的 XXX_operation::validate()
	*/
   for( const auto& op : operations )
      operation_validate(op);
}

//生成交易id
gamebank::protocol::transaction_id_type gamebank::protocol::transaction::id() const
{
  //本次交易摘要
   auto h = digest();
   transaction_id_type result;
   //摘要的hash --> 交易的hash == 交易id
   memcpy(result._hash, h._hash, std::min(sizeof(result), sizeof(h)));
   return result;
}

//生成交易签名，返回最新签名的引用
const signature_type& gamebank::protocol::signed_transaction::sign(const private_key_type& key, const chain_id_type& chain_id)
{
   //对特定区块链ID和本次交易进行序列化，返回加密后的摘要
   digest_type h = sig_digest( chain_id );
   //私钥生成签名后加到signatures就完事
   signatures.push_back(key.sign_compact(h));
   return signatures.back();
}

//序列化交易摘要，生成签名(signature_type)
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

void transaction::get_required_authorities( flat_set< account_name_type >& active,	//操作所需的active授权账号
                                            flat_set< account_name_type >& owner,		   //操作所需的owner授权账号
                                            flat_set< account_name_type >& posting,		   //操作所需的posting授权账号
                                            vector< authority >& other )const
{
//得到交易所需要的相关账户
   for( const auto& op : operations )
      operation_get_required_authorities( op, active, owner, posting, other );
}

//提取公钥，用于验证交易签名
flat_set<public_key_type> signed_transaction::get_signature_keys( const chain_id_type& chain_id )const
{ try {
   auto d = sig_digest( chain_id );
   flat_set<public_key_type> result;
   //获取所有用于验证交易签名的公钥
   for( const auto&  sig : signatures )
   {
      GAMEBANK_ASSERT(
		 //通过签名和交易摘要(digest)抽取公钥
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

   //所有操作需要的授权账号
   get_required_authorities( required_active, required_owner, required_posting, other );

   /** posting authority cannot be mixed with active authority in same transaction */

   //有Post授权需要
   if( required_posting.size() ) {
   	  //get_signature_keys( chain_id )： 已签名的公钥
   	  //available_keys:钱包存在的公钥
      sign_state s(get_signature_keys( chain_id ),get_posting,available_keys);
      s.max_recursion = max_recursion_depth;

	  //不能有owner和active授权需要？？
      FC_ASSERT( !required_owner.size() );
      FC_ASSERT( !required_active.size() );

	  //判断每一个post账户是否有足够的key
      for( auto& posting : required_posting )
         s.check_authority( posting  );

	  //更新provided_signatures
      s.remove_unused_signatures();

	  //返回钱包中存在的公钥许可
      set<public_key_type> result;

      for( auto& provided_sig : s.provided_signatures )
         if( available_keys.find( provided_sig.first ) != available_keys.end() )
            result.insert( provided_sig.first );

      return result;
   }

//不需要post授权,只需要owner或者active

   //存储签名状态
   //get_signature_keys( chain_id )： 已签名的公钥
   //available_keys:钱包存在的公钥
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
   const chain_id_type& chain_id,							//区块链id
   const flat_set< public_key_type >& available_keys,		//钱包中存在的权限公钥许可

   //typedef std::function<authority(const string&)> authority_getter;
   const authority_getter& get_active,
   const authority_getter& get_owner,
   const authority_getter& get_posting,
   uint32_t max_recursion
   ) const
{
   //s: 钱包中存在的公钥许可《需要签名的公钥》
   set< public_key_type > s = get_required_signatures( chain_id, available_keys, get_active, get_owner, get_posting, max_recursion );
   flat_set< public_key_type > result( s.begin(), s.end() );

   for( const public_key_type& k : s )
   {
      result.erase( k );
      try
      {
//do check authority，如果成功，则不需要签名
         gamebank::protocol::verify_authority( operations, result, get_active, get_owner, get_posting, max_recursion );
         continue;  // element stays erased if verify_authority is ok
      }
	  //认证失败
      catch( const tx_missing_owner_auth& e ) {}
      catch( const tx_missing_active_auth& e ) {}
      catch( const tx_missing_posting_auth& e ) {}
      catch( const tx_missing_other_auth& e ) {}
      result.insert( k );
   }
   //返回需要签名的公钥
   return set<public_key_type>( result.begin(), result.end() );
}

//验证签名(在_apply_transcation中调用)
void signed_transaction::verify_authority(
   const chain_id_type& chain_id,
   const authority_getter& get_active,
   const authority_getter& get_owner,
   const authority_getter& get_posting,
   uint32_t max_recursion )const
{ try {
//从交易摘要提取公钥，验证交易签名
   gamebank::protocol::verify_authority( operations, get_signature_keys( chain_id ), get_active, get_owner, get_posting, max_recursion );
} FC_CAPTURE_AND_RETHROW( (*this) ) }

} } // gamebank::protocol
