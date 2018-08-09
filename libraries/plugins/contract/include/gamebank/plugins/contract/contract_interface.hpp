#pragma once

#include <gamebank/protocol/types.hpp>
#include <fc/variant.hpp>

namespace gamebank { namespace plugins { namespace contract {

	using gamebank::protocol::account_name_type;
	using fc::variant;
	using fc::variants;

	class contract_interface {
	public:
		contract_interface(account_name_type n) :name(n) {}

		virtual bool deploy(const std::string& data) = 0;

		virtual bool call_method(const std::string& method, const variants& args, std::string& result) = 0;


		account_name_type name;
	};

}}}