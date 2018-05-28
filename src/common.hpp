#pragma once

#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <climits>

#include <inttypes.h>

#define UNREACHABLE_CODE(x) do { __builtin_unreachable(); } while (0)
#define UNUSED(x) (void)(x)
