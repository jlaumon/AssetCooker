/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "RemoteControl.h"
#include "App.h"
#include "Debug.h"

#include <CookingSystem.h>
#include <Bedrock/UniquePtr.h>
#include <Bedrock/Event.h>
#include <Bedrock/Thread.h>
#include <subprocess/subprocess.h>

#include <win32/file.h>
#include <win32/process.h>
#include <win32/window.h>
#include <win32/threads.h>

extern "C"
{
	BOOL WINAPI SetFocus(HWND hWnd);
	BOOL WINAPI SetForegroundWindow(HWND hWnd);
}

template <class taType>
using MappedPtr = UniquePtr<taType, [](taType* inPtr) { UnmapViewOfFile(inPtr); }>;

namespace 
{

	struct AssetCookerSharedMemory
	{
		uint32 mVersion;
		uint32 mProcessID;
	};


	enum class Action
	{
		Kill,
		Pause,
		Unpause,
		ShowWindow,
		_Count,
	};


	enum class Status
	{
		IsPaused,
		IsIdle,
		HasErrors,
		_Count,
	};


	struct RemoteControl
	{
		OwnedHandle						   mSharedMemoryHandle;
		MappedPtr<AssetCookerSharedMemory> mSharedMemoryPtr;

		OwnedHandle						   mActionEvents[(int)Action::_Count];
		OwnedHandle						   mStatusEvents[(int)Status::_Count];

		OwnedHandle&					   GetEvent(Action inAction) { return mActionEvents[(int)inAction]; }
		OwnedHandle&					   GetEvent(Status inStatus) { return mStatusEvents[(int)inStatus]; }
	};

}

StringView gToStringView(Action inEnum)
{
	static constexpr StringView str[] =
	{
		"Kill",
		"Pause",
		"Unpause",
		"ShowWindow",
	};
	static_assert((int)gElemCount(str) == (int)Action::_Count);
	return str[(int)inEnum];
}


StringView gToStringView(Status inEnum)
{
	static constexpr StringView str[] =
	{
		"IsPaused",
		"IsIdle",
		"HasErrors",
	};
	static_assert((int)gElemCount(str) == (int)Status::_Count);
	return str[(int)inEnum];
}


RemoteControl gRemoteControl;
Thread		  gRemoteControlThread;


static HANDLE sCreateSharedEvent(StringView inAssetCookerID, StringView inEventName, Event::ResetMode inResetMode)
{
	TempString name = inAssetCookerID;
	name += " ";
	name += inEventName;

	HANDLE event = CreateEventA(nullptr, inResetMode == Event::ManualReset, FALSE, name.AsCStr());
	if (event == nullptr)
	{
		gAppLogError("RemoteControl Init Failed - CreateEventA failed for %s - %s", 
			name.AsCStr(),
			GetLastErrorString().AsCStr());
	}

	return event;
}


void gRemoteControlInit(StringView inAssetCookerID)
{
	// Initialize the shared memory.
	{
		TempString shared_memory_name = inAssetCookerID;
		shared_memory_name += " SharedMemory";

		constexpr int cSharedMemorySize	= (int)sizeof(AssetCookerSharedMemory);

		gRemoteControl.mSharedMemoryHandle = CreateFileMappingA(
			INVALID_HANDLE_VALUE,
			nullptr,
			PAGE_READWRITE, 
			0, cSharedMemorySize,
			shared_memory_name.AsCStr());

		if (gRemoteControl.mSharedMemoryHandle == nullptr)
		{
			gAppLogError("RemoteControl Init Failed - OpenFileMappingA failed for %s - %s", 
				shared_memory_name.AsCStr(),
				GetLastErrorString().AsCStr());
			goto init_error;
		}

		gRemoteControl.mSharedMemoryPtr = MappedPtr<AssetCookerSharedMemory>((AssetCookerSharedMemory*)MapViewOfFile(
			gRemoteControl.mSharedMemoryHandle, 
			FILE_MAP_READ | FILE_MAP_WRITE, 
			0, 0, cSharedMemorySize));

		if (gRemoteControl.mSharedMemoryPtr == nullptr)
		{
			gAppLogError("RemoteControl Init Failed - MapViewOfFile failed - %s", GetLastErrorString().AsCStr());
			goto init_error;
		}

		// Initialize the shared memory.
		gRemoteControl.mSharedMemoryPtr->mVersion	= 0;
		gRemoteControl.mSharedMemoryPtr->mProcessID = GetCurrentProcessId();
	}

	// Open the shared events.
	for (int i = 0; i < (int)Action::_Count; i++)
	{
		gRemoteControl.mActionEvents[i] = sCreateSharedEvent(
			inAssetCookerID, 
			gToStringView((Action)i), 
			Event::AutoReset);

		if (gRemoteControl.mActionEvents[i] == nullptr)
			goto init_error;
	}

	for (int i = 0; i < (int)Status::_Count; i++)
	{
		gRemoteControl.mStatusEvents[i] = sCreateSharedEvent(
			inAssetCookerID, 
			gToStringView((Status)i), 
			Event::AutoReset);

		if (gRemoteControl.mStatusEvents[i] == nullptr)
			goto init_error;
	}

	// IsIdle and HasErrors are always false at this point, but IsPaused might already be set to true. Update if necessary.
	if (gCookingSystem.IsCookingPaused())
		gRemoteControlOnIsPausedChange(true);

	gRemoteControlThread.Create({ "RemoteControl", 16_KiB, 0_KiB, EThreadPriority::AboveNormal }, [](Thread& inThread)
	{
		while (true)
		{
			// Wait until one of the events gets signaled.
			DWORD result = WaitForMultipleObjects(
				gElemCount(gRemoteControl.mActionEvents), 
				&gRemoteControl.mActionEvents[0].mHandle, 
				FALSE, INFINITE);

			if (inThread.IsStopRequested())
				return;

			if (result < WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + gElemCount(gRemoteControl.mActionEvents)) [[unlikely]]
			{
				gAppLogError("RemoteControl Thread Failed - WaitForMultipleObjects returned %u - %s",
					result, GetLastErrorString().AsCStr());

				// Could we continue? Not sure, depends on the error?
				// Exit the thread for now, hopefully will never happen.
				return;
			}

			Action action = (Action)(result - WAIT_OBJECT_0);
			bool   action_valid = true;

			switch (action)
			{
			case Action::Kill:
				gApp.RequestExit();
				break;

			case Action::Pause:
				gCookingSystem.SetCookingPaused(true);
				break;

			case Action::Unpause:
				gCookingSystem.SetCookingPaused(false);
				break;

			case Action::ShowWindow:
				ShowWindow(gApp.mMainWindowHwnd, SW_RESTORE);   // Restore the window (if minimized or hidden).
				SetFocus(gApp.mMainWindowHwnd);                 // Set the focus on the window.
				SetForegroundWindow(gApp.mMainWindowHwnd);      // And bring it to the foreground.
				break;

			default:
				action_valid = false;
				break;
			}

			if (action_valid)
				gAppLog("RemoteControl received Action %s.", gToStringView(action).AsCStr());
			else
				gAppLog("RemoteControl received Unknown Action.");
		}
	});

	return;

	init_error:
	gRemoteControlExit();
}


void gRemoteControlExit()
{
	if (gRemoteControlThread.IsJoinable())
	{
		gRemoteControlThread.RequestStop();
		SetEvent(gRemoteControl.GetEvent(Action::Kill)); // Set any event to wake up the thread.
		gRemoteControlThread.Join();
	}

	// Reset all the status events on exit to avoid leaving an inconsistent state.
	for (int i = 0; i < (int)Status::_Count; i++)
		ResetEvent(gRemoteControl.mStatusEvents[i]);

	gRemoteControl = {};
}


static void sUpdateStatusEvent(Status inStatus, bool inSet)
{
	OwnedHandle& event = gRemoteControl.GetEvent(inStatus);
	if (!event.IsValid())
		return; // Not initialized, or init failed?

	if (inSet)
		SetEvent(event);
	else
		ResetEvent(event);
}


void gRemoteControlOnIsPausedChange(bool inIsPaused)
{
	sUpdateStatusEvent(Status::IsPaused, inIsPaused);
}


void gRemoteControlOnIsIdleChange(bool inIsIdle)
{
	sUpdateStatusEvent(Status::IsIdle, inIsIdle);
}


void gRemoteControlOnHasErrorsChange(bool inHasErrors)
{
	sUpdateStatusEvent(Status::HasErrors, inHasErrors);
}