#pragma once

#include "Core.h"


struct CookingRuleID
{
	int16 mIndex = -1;

	bool  IsValid() const { return *this != cInvalid(); }
	static constexpr CookingRuleID cInvalid() { return {}; }
	auto  operator<=>(const CookingRuleID& inOther) const = default;
};

template <> struct ankerl::unordered_dense::hash<CookingRuleID> : MemoryHasher<CookingRuleID> {};

struct CookingCommandID
{
	uint32 mIndex = (uint32)-1;

	bool  IsValid() const { return *this != cInvalid(); }
	static constexpr CookingCommandID cInvalid() { return {}; }
	auto  operator<=>(const CookingCommandID& inOther) const = default;
};

template <> struct ankerl::unordered_dense::hash<CookingCommandID> : MemoryHasher<CookingCommandID> {};
