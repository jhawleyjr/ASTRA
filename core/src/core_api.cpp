#include "astra/core_api.hpp"

namespace astra {

std::uint32_t core_api_version() noexcept { return kCoreApiVersion; }

const char* built_with() noexcept { return kBuiltWith; }

}  // namespace astra
