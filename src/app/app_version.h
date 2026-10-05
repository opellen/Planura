#pragma once

#include <string_view>

namespace plnr {

// Single source of truth for the app version (window title, .plr meta.appVersion).
// Bump PATCH by 1 with every AI source-change batch before rebuild.
// Never a second kAppVersion definition anywhere (ODR merge risk).
inline constexpr std::string_view kAppVersion = "0.1.67";

}  // namespace plnr
