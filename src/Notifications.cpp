/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "Notifications.h"
#include "App.h"
#include <windows.h>
#include "xxHash/xxh3.h"


static GUID sNotifGUID;

void gNotifInit(void* inHwnd)
{
	// Hash the asset cooker ID.
	TempString		asset_cooker_id		 = gGetAssetCookerIdentifier(gApp.mConfigFilePath);
	XXH128_hash_t	asset_cooker_id_hash = XXH3_128bits(asset_cooker_id.Data(), asset_cooker_id.Size());

	// Make a GUID out of the hash.
	// It's not a proper GUID, but it's good enough. We care about:
	// - having the same GUID when restarting AC (to avoid accumulating zombie tray icons)
	// - having a different GUID for multiple AC instances that use a different config.toml (might want to cook multiple projects at once)
	static_assert(sizeof(asset_cooker_id_hash) == sizeof(sNotifGUID));
	gMemCopy(&sNotifGUID, &asset_cooker_id_hash, sizeof(asset_cooker_id_hash));

	NOTIFYICONDATAA nid  = {};
	nid.cbSize           = sizeof(nid);
	nid.hWnd             = (HWND)inHwnd;
	nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP | NIF_GUID;
	nid.guidItem         = sNotifGUID;
	nid.hIcon            = LoadIconA(GetModuleHandleA(nullptr), "chef_hat_heart");
	nid.uCallbackMessage = cNotifCallbackID;

	// Set the app title as default tooltip.
	gStringCopy(nid.szTip, gApp.mMainWindowTitle);

	bool ret = Shell_NotifyIconA(NIM_ADD, &nid);
	// If the program is killed without letting it clean up its icon, the next launch's add will fail.
	// Deleting that "zombie" icon first before recreating it allows to "refresh" the icon ourselves.
	if (!ret)
	{
		ret = Shell_NotifyIconA(NIM_DELETE, &nid);
		gAssert(ret);
		ret = Shell_NotifyIconA(NIM_ADD, &nid);
	}
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
	nid.guidItem        = sNotifGUID;

	bool ret = Shell_NotifyIconA(NIM_DELETE, &nid);
	gAssert(ret);
}


void gNotifAdd(NotifType inType, StringView inTitle, StringView inMessage)
{
	NOTIFYICONDATAA nid = {};
	nid.cbSize          = sizeof(nid);
	nid.uFlags          = NIF_GUID | NIF_INFO;
	nid.guidItem        = sNotifGUID;

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

	gAppLog("Notification: %s - %s %s", gToStringView(inType).AsCStr(), inTitle.AsCStr(), inMessage.AsCStr());

	bool ret = Shell_NotifyIconA(NIM_MODIFY, &nid);
	gAssert(ret);
}


void gNotifSetToolTip(StringView inMessage)
{
	NOTIFYICONDATAA nid = {};
	nid.cbSize          = sizeof(nid);
	nid.uFlags          = NIF_GUID | NIF_SHOWTIP;
	nid.guidItem        = sNotifGUID;

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