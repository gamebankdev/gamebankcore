#pragma once
#include <gamebank/protocol/base.hpp>

namespace gamebank { namespace protocol {

   struct block_header
   {
      digest_type                   digest()const;
      block_id_type                 previous;
      uint32_t                      block_num()const { return num_from_id(previous) + 1; }
      fc::time_point_sec            timestamp;
      string                        witness;					//	产生该区块的见证人
      checksum_type                 transaction_merkle_root;	//	交易merkle root:数字指纹
      block_header_extensions_type  extensions;

      static uint32_t num_from_id(const block_id_type& id);
   };

   struct signed_block_header : public block_header
   {
      block_id_type              id()const;
      fc::ecc::public_key        signee()const;
      void                       sign( const fc::ecc::private_key& signer );
      bool                       validate_signee( const fc::ecc::public_key& expected_signee )const;

      signature_type             witness_signature;
   };


} } // gamebank::protocol

FC_REFLECT( gamebank::protocol::block_header, (previous)(timestamp)(witness)(transaction_merkle_root)(extensions) )
FC_REFLECT_DERIVED( gamebank::protocol::signed_block_header, (gamebank::protocol::block_header), (witness_signature) )
