#pragma once

namespace fc {
class uint128;
class variant;
} // fc

namespace gamebank { namespace protocol {
template< typename Storage = fc::uint128 >
class fixed_string_impl;

class asset_symbol_type;
class legacy_gamebank_asset_symbol_type;
struct legacy_gamebank_asset;
} } // gamebank::protocol

namespace fc { namespace raw {

template<typename Stream>
inline void pack( Stream& s, const uint128& u );
template<typename Stream>
inline void unpack( Stream& s, uint128& u );

template< typename Stream, typename Storage >
inline void pack( Stream& s, const gamebank::protocol::fixed_string_impl< Storage >& u );
template< typename Stream, typename Storage >
inline void unpack( Stream& s, gamebank::protocol::fixed_string_impl< Storage >& u );

template< typename Stream >
inline void pack( Stream& s, const gamebank::protocol::asset_symbol_type& sym );
template< typename Stream >
inline void unpack( Stream& s, gamebank::protocol::asset_symbol_type& sym );

template< typename Stream >
inline void pack( Stream& s, const gamebank::protocol::legacy_gamebank_asset_symbol_type& sym );
template< typename Stream >
inline void unpack( Stream& s, gamebank::protocol::legacy_gamebank_asset_symbol_type& sym );

} // raw

template< typename Storage >
inline void to_variant( const gamebank::protocol::fixed_string_impl< Storage >& s, fc::variant& v );
template< typename Storage >
inline void from_variant( const variant& v, gamebank::protocol::fixed_string_impl< Storage >& s );

inline void to_variant( const gamebank::protocol::asset_symbol_type& sym, fc::variant& v );

inline void from_variant( const fc::variant& v, gamebank::protocol::legacy_gamebank_asset& leg );
inline void to_variant( const gamebank::protocol::legacy_gamebank_asset& leg, fc::variant& v );

} // fc
