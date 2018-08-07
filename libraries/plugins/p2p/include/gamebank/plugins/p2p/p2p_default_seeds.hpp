#pragma once

#include <vector>

namespace gamebank{ namespace plugins { namespace p2p {

#ifdef IS_TEST_NET
const std::vector< std::string > default_seeds;
#else
const std::vector< std::string > default_seeds = {
   "seed.gb.cool:2001"          // gamebank
};
#endif

} } } // gamebank::plugins::p2p
