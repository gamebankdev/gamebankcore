#pragma once

#include <gamebank/chain/contract/contract_interface.hpp>
#include <gamebank/chain/database.hpp>

// 50M
#define GAMEBANK_CONTRACT_MAX_MEMORY 1024*50

namespace gamebank { namespace chain {

	namespace detail { class contract_lua_impl; }

	class contract_lua : public contract_interface {
	public:
		contract_lua(account_name_type n);
		~contract_lua();

		virtual bool deploy(const std::string& data);
		bool load(const std::string& data);

		virtual bool call_method(const std::string& method, const variants& args, std::string& result);

		void set_database(chain::database* db);
		void set_abi(const std::set<std::string>& method_names);
		void set_extend(const account_name_type& contract_name, const account_name_type& caller_name );
		void set_extend_arg(int memory_limit, int opcode_limit);
		int get_current_opcount();

	private:
		std::unique_ptr< detail::contract_lua_impl > my;

	};

}}