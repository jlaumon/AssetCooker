
// Lua triggers this warning several times when calculating node sizes with '1<<size'. It's fine, a 64-bit shift was not intended.
// warning C4334: '<<': result of 32-bit shift implicitly converted to 64 bits (was 64-bit shift intended?)
#pragma warning( disable : 4334 )

#define LUA_IMPL
#include "minilua.h"