#pragma once

#include "contract_interface.hpp"
extern "C"
{
#include "lua/lua.h"
}

namespace gamebank { namespace contract {

	class contract_lua : public contract_interface {
	public:
		contract_lua(account_name_type n);
		~contract_lua();

		virtual bool deploy(const std::string& data);

		virtual bool call_method(const std::string& method, const variants& args, std::string& result);
	
		void save_modified_data();

	private:
		lua_State* L = nullptr;

	};

}}