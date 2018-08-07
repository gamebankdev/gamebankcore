#pragma once

#include <fc/io/raw.hpp>
#include <gamebank/protocol/types_fwd.hpp>

#define GAMEBANK_ASSET_SYMBOL_PRECISION_BITS    4
#define GB_MAX_NAI                          99999999
#define GB_MIN_NAI                          1
#define GB_MIN_NON_RESERVED_NAI             10000000
#define GAMEBANK_ASSET_SYMBOL_NAI_LENGTH        10
#define GAMEBANK_ASSET_SYMBOL_NAI_STRING_LENGTH ( GAMEBANK_ASSET_SYMBOL_NAI_LENGTH + 2 )

#define GAMEBANK_PRECISION_GBD   (3)
#define GAMEBANK_PRECISION_GBC (3)
#define GAMEBANK_PRECISION_GBS (6)

// One's place is used for check digit, which means NAI 0-9 all have NAI data of 0 which is invalid
// This space is safe to use because it would alwasys result in failure to convert from NAI
#define GAMEBANK_NAI_GBD   (1)
#define GAMEBANK_NAI_GBC (2)
#define GAMEBANK_NAI_GBS (3)

#define GAMEBANK_ASSET_NUM_GBD \
  (((GB_MAX_NAI + GAMEBANK_NAI_GBD)   << GAMEBANK_ASSET_SYMBOL_PRECISION_BITS) | GAMEBANK_PRECISION_GBD)
#define GAMEBANK_ASSET_NUM_GBC \
  (((GB_MAX_NAI + GAMEBANK_NAI_GBC) << GAMEBANK_ASSET_SYMBOL_PRECISION_BITS) | GAMEBANK_PRECISION_GBC)
#define GAMEBANK_ASSET_NUM_GBS \
  (((GB_MAX_NAI + GAMEBANK_NAI_GBS) << GAMEBANK_ASSET_SYMBOL_PRECISION_BITS) | GAMEBANK_PRECISION_GBS)


#define GBS_SYMBOL_U64  (uint64_t('G') | (uint64_t('B') << 8) | (uint64_t('S') << 16))
#define GBC_SYMBOL_U64  (uint64_t('G') | (uint64_t('B') << 8) | (uint64_t('C') << 16))
#define GBD_SYMBOL_U64    (uint64_t('G') | (uint64_t('B') << 8) | (uint64_t('D') << 16))

#define GBS_SYMBOL_SER  (uint64_t(6) | (GBS_SYMBOL_U64 << 8)) ///< GBS with 6 digits of precision
#define GBC_SYMBOL_SER  (uint64_t(3) | (GBC_SYMBOL_U64 << 8)) ///< GBC with 3 digits of precision
#define GBD_SYMBOL_SER    (uint64_t(3) |   (GBD_SYMBOL_U64 << 8)) ///< GBD with 3 digits of precision

#define GAMEBANK_ASSET_MAX_DECIMALS 12

#define GB_ASSET_NUM_VESTING_MASK     0x20

namespace gamebank { namespace protocol {

class asset_symbol_type
{
   public:
      enum asset_symbol_space
      {
         legacy_space = 1
      };

      asset_symbol_type() {}

      // buf must have space for GAMEBANK_ASSET_SYMBOL_MAX_LENGTH+1
      static asset_symbol_type from_string( const std::string& str );
      static asset_symbol_type from_nai_string( const char* buf, uint8_t decimal_places );
      static asset_symbol_type from_asset_num( uint32_t asset_num )
      {   asset_symbol_type result;   result.asset_num = asset_num;   return result;   }
      static uint32_t asset_num_from_nai( uint32_t nai, uint8_t decimal_places );
      static asset_symbol_type from_nai( uint32_t nai, uint8_t decimal_places )
      {   return from_asset_num( asset_num_from_nai( nai, decimal_places ) );          }

      std::string to_string()const;

      void to_nai_string( char* buf )const;
      std::string to_nai_string()const
      {
         char buf[ GAMEBANK_ASSET_SYMBOL_NAI_STRING_LENGTH ];
         to_nai_string( buf );
         return std::string( buf );
      }

      uint32_t to_nai()const;

      /**Returns true when symbol represents vesting variant of the token,
       * false for liquid one.
       */
      bool is_vesting() const;
      /**Returns vesting symbol when called from liquid one
       * and liquid symbol when called from vesting one.
       * Returns back the GBD symbol if represents GBD.
       */
      asset_symbol_type get_paired_symbol() const;

      asset_symbol_space space()const;
      uint8_t decimals()const
      {  return uint8_t( asset_num & 0x0F );    }
      void validate()const;

      friend bool operator == ( const asset_symbol_type& a, const asset_symbol_type& b )
      {  return (a.asset_num == b.asset_num);   }
      friend bool operator != ( const asset_symbol_type& a, const asset_symbol_type& b )
      {  return (a.asset_num != b.asset_num);   }
      friend bool operator <  ( const asset_symbol_type& a, const asset_symbol_type& b )
      {  return (a.asset_num <  b.asset_num);   }
      friend bool operator >  ( const asset_symbol_type& a, const asset_symbol_type& b )
      {  return (a.asset_num >  b.asset_num);   }
      friend bool operator <= ( const asset_symbol_type& a, const asset_symbol_type& b )
      {  return (a.asset_num <= b.asset_num);   }
      friend bool operator >= ( const asset_symbol_type& a, const asset_symbol_type& b )
      {  return (a.asset_num >= b.asset_num);   }

      uint32_t asset_num = 0;
};

} } // gamebank::protocol

FC_REFLECT(gamebank::protocol::asset_symbol_type, (asset_num))

namespace fc { namespace raw {

// Legacy serialization of assets
// 0000pppp aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee ffffffff 00000000
// Symbol = abcdef
//
// NAI serialization of assets
// aaa1pppp bbbbbbbb cccccccc dddddddd
// NAI = (MSB to LSB) dddddddd cccccccc bbbbbbbb aaa
//
// NAI internal storage of legacy assets

template< typename Stream >
inline void pack( Stream& s, const gamebank::protocol::asset_symbol_type& sym )
{
   switch( sym.space() )
   {
      case gamebank::protocol::asset_symbol_type::legacy_space:
      {
         uint64_t ser = 0;
         switch( sym.asset_num )
         {
            case GAMEBANK_ASSET_NUM_GBC:
               ser = GBC_SYMBOL_SER;
               break;
            case GAMEBANK_ASSET_NUM_GBD:
               ser = GBD_SYMBOL_SER;
               break;
            case GAMEBANK_ASSET_NUM_GBS:
               ser = GBS_SYMBOL_SER;
               break;
            default:
               FC_ASSERT( false, "Cannot serialize unknown asset symbol" );
         }
         pack( s, ser );
         break;
      }
      
      default:
         FC_ASSERT( false, "Cannot serialize unknown asset symbol" );
   }
}

template< typename Stream >
inline void unpack( Stream& s, gamebank::protocol::asset_symbol_type& sym )
{
   uint64_t ser = 0;
   s.read( (char*) &ser, 4 );

   switch( ser )
   {
      case GBC_SYMBOL_SER & 0xFFFFFFFF:
         s.read( ((char*) &ser)+4, 4 );
         FC_ASSERT( ser == GBC_SYMBOL_SER, "invalid asset bits" );
         sym.asset_num = GAMEBANK_ASSET_NUM_GBC;
         break;
      case GBD_SYMBOL_SER & 0xFFFFFFFF:
         s.read( ((char*) &ser)+4, 4 );
         FC_ASSERT( ser == GBD_SYMBOL_SER, "invalid asset bits" );
         sym.asset_num = GAMEBANK_ASSET_NUM_GBD;
         break;
      case GBS_SYMBOL_SER & 0xFFFFFFFF:
         s.read( ((char*) &ser)+4, 4 );
         FC_ASSERT( ser == GBS_SYMBOL_SER, "invalid asset bits" );
         sym.asset_num = GAMEBANK_ASSET_NUM_GBS;
         break;
      default:
         sym.asset_num = uint32_t( ser );
   }
   sym.validate();
}

} // fc::raw

inline void to_variant( const gamebank::protocol::asset_symbol_type& sym, fc::variant& var )
{
   try
   {
      std::vector< variant > v( 2 );
      v[0] = sym.decimals();
      v[1] = sym.to_nai_string();
   } FC_CAPTURE_AND_RETHROW()
}

inline void from_variant( const fc::variant& var, gamebank::protocol::asset_symbol_type& sym )
{
   try
   {
      auto v = var.as< std::vector< variant > >();
      FC_ASSERT( v.size() == 2, "Expected tuple of length 2." );

      sym = gamebank::protocol::asset_symbol_type::from_nai_string( v[1].as< std::string >().c_str(), v[0].as< uint8_t >() );
   } FC_CAPTURE_AND_RETHROW()
}

} // fc
