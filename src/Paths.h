#pragma once

#include "Core.h"
#include "Strings.h"

// Replaces / by \.
constexpr MutStringView gNormalizePath(MutStringView ioPath)
{
	for (char& c : ioPath)
	{
		if (c == '/')
			c = '\\';
	}

	return ioPath;
}

constexpr bool gIsNormalized(StringView inPath)
{
	for (char c : inPath)
	{
		if (c == '/')
			return false;
	}

	return true;
}

constexpr bool gIsAbsolute(StringView inPath)
{
	return inPath.size() >= 3 && gIsAlpha(inPath[0]) && inPath[1] == ':' && (inPath[2] == '\\' || inPath[2] == '/'); 
}


constexpr bool gIsRelative(StringView inPath)
{
	return inPath.Contains(".\\") || inPath.Contains("./"); // Note: That will also return true for ../
}