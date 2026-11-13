#pragma once

// GCC 7 ships std::experimental::filesystem instead of std::filesystem.
// Keep a single alias so call sites do not need compiler-specific code.
#if defined(__has_include)
#  if __has_include(<filesystem>)
#    include <filesystem>
#    define NKR_HAVE_STD_FILESYSTEM 1
#  endif
#endif

#ifndef NKR_HAVE_STD_FILESYSTEM
#  include <experimental/filesystem>
#endif

namespace nkr {

#ifdef NKR_HAVE_STD_FILESYSTEM
namespace fs = std::filesystem;
#else
namespace fs = std::experimental::filesystem;
#endif

} // namespace nkr
