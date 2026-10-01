#pragma once

// Lua's own configuration must see a library build before its header guard is
// set. Predeclare the public API with C linkage while compiling its VM as C++.
#include "lua/lprefix.h"
#define LUA_LIB
#include "lua/lua.hpp"
#undef LUA_LIB
