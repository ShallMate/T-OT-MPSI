#pragma once

#if !defined(__GNUC__) && !defined(__clang__)
#error "OT-MPSI requires a compiler with native 128-bit integer support"
#endif

using int128_t = __int128_t;
using uint128_t = __uint128_t;
