/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Notifications.h"
#include "App.h"
#include <windows.h>

// Lazy way of turning a string into a compile time GUID.
class __declspec(uuid("cbdb5e17-ea19-4444-9947-7abf64f8c203")) NotifClass;
constexpr GUID cNotifGUID = __uuidof(NotifClass);


void gNotifInit(void* inHwnd)
{
	NOTIFYICONDATAA nid  = {};
	nid.cbSize           = sizeof(nid);
	nid.hWnd             = (HWND)inHwnd;
	nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP | NIF_GUID;
	nid.guidItem         = cNotifGUID;
	nid.hIcon            = LoadIconA(GetModuleHandleA(nullptr), "chef_hat_heart");
	nid.uCallbackMessage = cNotifCallbackID;

	// Set the app title as default tooltip.
	gStringCopy(nid.szTip, gApp.mMainWindowTitle);

	bool ret = Shell_NotifyIconA(NIM_ADD, &nid);
	gAssert(ret);

	// NOTIFYICON_VERSION_4 is prefered
	nid.uVersion = NOTIFYICON_VERSION_4;
	ret          = Shell_NotifyIconA(NIM_SETVERSION, &nid);
	gAssert(ret);
}


void gNotifExit()
{
	NOTIFYICONDATAA nid = {};
	nid.cbSize          = sizeof(nid);
	nid.uFlags          = NIF_GUID;
	nid.guidItem        = cNotifGUID;

	bool ret = Shell_NotifyIconA(NIM_DELETE, &nid);
	gAssert(ret);
}


void gNotifAdd(NotifType inType, StringView inTitle, StringView inMessage)
{
	NOTIFYICONDATAA nid = {};
	nid.cbSize          = sizeof(nid);
	nid.uFlags          = NIF_GUID | NIF_INFO;
	nid.guidItem        = cNotifGUID;

	nid.dwInfoFlags     = 0;
	switch (inType)
	{
	case NotifType::Info:
		nid.dwInfoFlags |= NIIF_INFO;
		break;
	case NotifType::Warning:
		nid.dwInfoFlags |= NIIF_WARNING;
		break;
	case NotifType::Error:
		nid.dwInfoFlags |= NIIF_ERROR;
		break;
	}

	if (!gShouldNotify(gApp.mEnableNotifSound))
		nid.dwInfoFlags |= NIIF_NOSOUND;

	gStringCopy(nid.szInfoTitle, inTitle);
	gStringCopy(nid.szInfo, inMessage);

	gApp.Log("Notification: {} - {} {}", gToStringView(inType), inTitle, inMessage);

	bool ret = Shell_NotifyIconA(NIM_MODIFY, &nid);
	gAssert(ret);
}


void gNotifSetToolTip(StringView inMessage)
{
	NOTIFYICONDATAA nid = {};
	nid.cbSize          = sizeof(nid);
	nid.uFlags          = NIF_GUID | NIF_SHOWTIP;
	nid.guidItem        = cNotifGUID;

	gStringCopy(nid.szTip, inMessage);

	bool ret = Shell_NotifyIconA(NIM_MODIFY, &nid);
	gAssert(ret);
}


bool gShouldNotify(NotifEnabled inEnabledMode)
{
	switch (inEnabledMode)
	{
	default:
	case NotifEnabled::Never:
		return false;
	case NotifEnabled::WhenMinimized:
		return gApp.mMainWindowIsMinimized;
	case NotifEnabled::Always:
		return true;
	}
}