#pragma once

#include "Core.h"


struct CookingRuleID
{
	int16 mIndex = -1;

	bool  IsValid() const { return *this != cInvalid(); }
	static constexpr CookingRuleID cInvalid() { return {}; }
	auto  operator<=>(const CookingRuleID& inOther) const = default;
};

struct CookingCommandID
{
	uint32 mIndex = (uint32)-1;

	bool  IsValid() const { return *this != cInvalid(); }
	static constexpr CookingCommandID cInvalid() { return {}; }
	auto  operator<=>(const CookingCommandID& inOther) const = default;
};
