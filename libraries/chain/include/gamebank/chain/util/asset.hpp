#pragma once

#include <gamebank/protocol/asset.hpp>

namespace gamebank { namespace chain { namespace util {

using gamebank::protocol::asset;
using gamebank::protocol::price;

inline asset to_gbd( const price& p, const asset& gbc )
{
   FC_ASSERT( gbc.symbol == GBC_SYMBOL );
   if( p.is_null() )
      return asset( 0, GBD_SYMBOL );
   return gbc * p;
}

inline asset to_gbc( const price& p, const asset& gbd )
{
   FC_ASSERT( gbd.symbol == GBD_SYMBOL );
   if( p.is_null() )
      return asset( 0, GBC_SYMBOL );
   return gbd * p;
}

} } }
