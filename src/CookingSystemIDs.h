/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Core.h"


struct CookingRuleID
{
	int16 mIndex = -1;

	bool  IsValid() const { return *this != cInvalid(); }
	static constexpr CookingRuleID cInvalid() { return {}; }
	auto  operator<=>(const CookingRuleID& inOther) const = default;
};

template <> struct Hash<CookingRuleID>
{
	uint64 operator()(CookingRuleID inID) const { return gHash(inID.mIndex); }
};

struct CookingCommandID
{
	uint32 mIndex = (uint32)-1;

	bool  IsValid() const { return *this != cInvalid(); }
	static constexpr CookingCommandID cInvalid() { return {}; }
	auto  operator<=>(const CookingCommandID& inOther) const = default;
};

template <> struct Hash<CookingCommandID>
{
	uint64 operator()(CookingCommandID inID) const { return gHash(inID.mIndex); }
};

struct CookingLogEntryID
{
	uint32 mIndex = (uint32)-1;

	bool  IsValid() const { return *this != cInvalid(); }
	static constexpr CookingLogEntryID cInvalid() { return {}; }
	auto  operator<=>(const CookingLogEntryID& inOther) const = default;
};
