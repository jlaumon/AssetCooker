#include "Strings.h"

#include <string.h>
#include <mbstring.h>

// Same as gIsEqual but case-insensitive.
bool gIsEqualCI(StringView inString1, StringView inString2)
{
	if (inString1.size() != inString2.size())
		return false;

	return _mbsnicmp((const unsigned char*)inString1.data(), (const unsigned char*)inString2.data(), inString1.size()) == 0;
}

// Same as gStartsWith but case-insensitive.
bool gStartsWithCI(StringView inString, StringView inStart)
{
	if (inString.size() < inStart.size())
		return false;

	return _mbsnicmp((const unsigned char*)inString.data(), (const unsigned char*)inStart.data(), inStart.size()) == 0;
}

// Same as gEndsWith but case-insensitive.
bool gEndsWithCI(StringView inString, StringView inEnd)
{
	if (inString.size() < inEnd.size())
		return false;

	return _mbsnicmp((const unsigned char*)inString.data() + inString.size() - inEnd.size(), (const unsigned char*)inEnd.data(), inEnd.size()) == 0;
}