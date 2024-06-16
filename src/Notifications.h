/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "Strings.h"

constexpr uint32 cNotifCallbackID = 0x8001; //  ie. WM_APP + 1

enum class NotifType
{
	Info,
	Warning,
	Error,
	_Count,
};


void gNotifInit(void* inHwnd);
void gNotifExit();
void gNotifAdd(NotifType inType, StringView inTitle, StringView inMessage);
void gNotifSetToolTip(StringView inMessage);


enum class NotifEnabled : uint8
{
	Never,
	WhenMinimized,
	Always,
	_Count,
};

bool gShouldNotify(NotifEnabled inEnableMode);	// Return true if a notification with this mode should currently be added.


constexpr StringView gToStringView(NotifType inVar)
{
	constexpr StringView cStrings[]
	{
		"Info",
		"Warning",
		"Error",
	};
	static_assert(gElemCount(cStrings) == (size_t)NotifType::_Count);

	return cStrings[(int)inVar];
}


constexpr StringView gToStringView(NotifEnabled inVar)
{
	constexpr StringView cStrings[]
	{
		"Never",
		"When Minimized",
		"Always",
	};
	static_assert(gElemCount(cStrings) == (size_t)NotifEnabled::_Count);

	return cStrings[(int)inVar];
}

