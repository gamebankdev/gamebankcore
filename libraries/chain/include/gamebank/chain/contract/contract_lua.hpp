#pragma once

#include <gamebank/chain/contract/contract_interface.hpp>

namespace gamebank { namespace chain {

	namespace detail { class contract_lua_impl; }

	class contract_lua : public contract_interface {
	public:
		contract_lua(account_name_type n);
		~contract_lua();

		virtual bool deploy(const std::string& data);

		virtual bool call_method(const std::string& method, const variants& args, std::string& result);

	private:
		std::unique_ptr< detail::contract_lua_impl > my;

	};

}}