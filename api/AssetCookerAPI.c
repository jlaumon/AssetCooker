// SPDX-License-Identifier: Unlicense
#include "AssetCookerAPI.h"

#include <stdio.h>
#include <stdlib.h>
#include <mbstring.h>

#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#ifndef ASSERT_COOKER_ASSERT
#include <assert.h>
#define ASSERT_COOKER_ASSERT assert
#endif

typedef unsigned char uint8;
typedef unsigned long uint32;
typedef unsigned long long uint64;

#define gElemCount(arr) (sizeof(arr) / sizeof(arr[0])) 

#define STR_OUT(buffer) buffer, gElemCount(buffer)

enum EventReset
{
	EventReset_Manual,
	EventReset_Auto,
};

enum Events
{
	Event_Kill,
	Event_Pause,
	Event_Unpause,
	Event_IsPaused,
	Event_IsIdle,
	Event_Count,
};


typedef struct AssetCookerInternal
{
	HANDLE mProcessHandle;

	HANDLE mEventKill;
	HANDLE mEventPause;
	HANDLE mEventUnpause;
	HANDLE mEventShowWindow;
	HANDLE mEventIsPaused;
	HANDLE mEventIsIdle;
	HANDLE mEventHasErrors;
} AssetCookerInternal;


struct AssetCookerSharedMemory
{
	uint32 mVersion;
	uint32 mProcessID;
};
typedef struct AssetCookerSharedMemory AssetCookerSharedMemory;


static uint64 sHashStringFNV1a(const char* inString)
{
	uint64 hash = 0xcbf29ce484222325ULL;

	while (*inString)
	{
		uint8 c = *inString;
		inString++;

		hash ^= c;
		hash += (hash << 1) + (hash << 4) + (hash << 5) + (hash << 7) + (hash << 8) + (hash << 40);
	}

	return hash;
}


static BOOL sStrConcat(const char* inStr1, const char* inStr2, char* outBuffer, int inBufferSize)
{
	int str_len = _snprintf_s(outBuffer, inBufferSize, _TRUNCATE, "%s%s", inStr1, inStr2);

	ASSERT_COOKER_ASSERT(str_len >= 0);
	return str_len >= 0;
}


// Build a unique name to use with shared Win32 objects (Events, Mutex, etc.)
static int sGetAssetCookerIdentifier(const char* inConfigFilePath, char* outBuffer, int inBufferSize)
{
	// Get the absolute path of the config file.
	char config_file_abs_path[MAX_PATH];
	int size = GetFullPathNameA(inConfigFilePath, gElemCount(config_file_abs_path), config_file_abs_path, NULL);
	if (size == 0 || size > gElemCount(config_file_abs_path))
		return -1;

	// Make it lowercase.
	_mbslwr_s(config_file_abs_path, gElemCount(config_file_abs_path));

	// Replace all forward slashes by backward slashes
	{
		char* s = config_file_abs_path;
		while (*s)
		{
			if (*s == '/')
				*s = '\\';
			s++;
		}
	}

	// Hash the path because it may be too long (names of shared objects are limited to 260 chars).
	uint64 config_file_path_hash = sHashStringFNV1a(config_file_abs_path);

	// Add Asset Cooker in front.
	// Note: returns -1 when string gets truncated (but still writes a null terminated string in outBuffer)
	int str_len = _snprintf_s(outBuffer, inBufferSize, _TRUNCATE, "Asset Cooker %016llX", config_file_path_hash);

	ASSERT_COOKER_ASSERT(str_len >= 0);
	return str_len;
}


BOOL sFileExists(const char* inPath)
{
	DWORD attributes = GetFileAttributesA(inPath);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}


HANDLE sCreateSharedEvent(const char* inAssetCookerIdentifier, const char* inEventName, enum EventReset inResetMode)
{
	char event_name[MAX_PATH];
	sStrConcat(inAssetCookerIdentifier, inEventName, STR_OUT(event_name));

	return CreateEventA(NULL, inResetMode == EventReset_Manual, FALSE, event_name);
}


int AssetCooker_Launch(const char* inExePath, const char* inConfigFilePath, int inOptions, AssetCookerHandle* ouHandle)
{
	// Make sure the config file exists.
	if (!sFileExists(inConfigFilePath))
		return -1;

	// Get the asset cooker identifier.
	char asset_cooker_id[MAX_PATH];
	if (sGetAssetCookerIdentifier(inConfigFilePath, STR_OUT(asset_cooker_id)) < 0)
		return -1;

	// Allocate a handle.
	AssetCookerInternal* ac_handle = calloc(1, sizeof(AssetCookerInternal));

	// Create the shared events that will be used to communicate with Asset Cooker.
	{
		// Actions
		ac_handle->mEventKill		= sCreateSharedEvent(asset_cooker_id, " Kill", EventReset_Auto);
		ac_handle->mEventPause		= sCreateSharedEvent(asset_cooker_id, " Pause", EventReset_Auto);
		ac_handle->mEventUnpause	= sCreateSharedEvent(asset_cooker_id, " Unpause", EventReset_Auto);
		ac_handle->mEventShowWindow	= sCreateSharedEvent(asset_cooker_id, " ShowWindow", EventReset_Auto);

		// Statuses
		ac_handle->mEventIsPaused	= sCreateSharedEvent(asset_cooker_id, " IsPaused", EventReset_Manual);
		ac_handle->mEventIsIdle		= sCreateSharedEvent(asset_cooker_id, " IsIdle", EventReset_Manual);
		ac_handle->mEventHasErrors	= sCreateSharedEvent(asset_cooker_id, " HasErrors", EventReset_Manual);

		if (ac_handle->mEventKill		== NULL ||
			ac_handle->mEventPause		== NULL ||
			ac_handle->mEventUnpause	== NULL ||
			ac_handle->mEventShowWindow == NULL ||
			ac_handle->mEventIsPaused	== NULL ||
			ac_handle->mEventIsIdle		== NULL ||
			ac_handle->mEventHasErrors	== NULL)
		{
			goto launch_error;
		}
	}

	// If we want to start paused (or unpaused), set the event before even starting the Asset Cooker process.
	{
		if (inOptions & AssetCookerOption_StartUnpaused)
			AssetCooker_Pause(ac_handle, 0);

		if (inOptions & AssetCookerOption_StartPaused)
		AssetCooker_Pause(ac_handle, 1);
	}

	// Now we need to check if Asset Cooker is already running. We can do that by trying to open its shared memory file.
	{
		char shared_memory_name[MAX_PATH];
		if (!sStrConcat(asset_cooker_id, " SharedMemory", STR_OUT(shared_memory_name)))
			goto launch_error;

		HANDLE shared_memory_handle = OpenFileMappingA(FILE_MAP_READ, FALSE, shared_memory_name);
		if (shared_memory_handle != NULL)
		{
			// Shared memory exists, that means Asset Cooker is already running.
			// Get the process ID from the shared memory.
			AssetCookerSharedMemory* shared_memory_ptr = MapViewOfFile(
				shared_memory_handle, 
				FILE_MAP_READ, 
				0, 0, 
				sizeof(AssetCookerSharedMemory));

			if (shared_memory_ptr == NULL)
			{
				CloseHandle(shared_memory_handle);
				goto launch_error;
			}

			// Get the process ID.
			DWORD process_id = shared_memory_ptr->mProcessID;

			// Unmap & close the shared memory.
			UnmapViewOfFile(shared_memory_ptr);
			CloseHandle(shared_memory_handle);

			// Get the process handle from the ID.
			ac_handle->mProcessHandle = OpenProcess(SYNCHRONIZE, FALSE, process_id);
			if (ac_handle->mProcessHandle == NULL)
				goto launch_error;
		}
		else
		{
			// Shared memory does not exist, Asset Cooker is probably not running.
			// Launch it now.
			char command_line[MAX_PATH + 32];
			if (!sStrConcat("-config_file ", inConfigFilePath, STR_OUT(command_line)))
				goto launch_error;

			PROCESS_INFORMATION process_info;
			STARTUPINFOA startup_info = {0};
			startup_info.cb = sizeof(startup_info);

			if (inOptions & AssetCookerOption_StartMinimized)
			{
				startup_info.dwFlags |= STARTF_USESHOWWINDOW;
				startup_info.wShowWindow = SW_SHOWMINIMIZED;
			}

			BOOL success = CreateProcessA(
				inExePath,
				command_line,
				NULL,			// lpProcessAttributes, 
				NULL,			// lpThreadAttributes,
				FALSE,			// bInheritHandles,
				0,				// dwCreationFlags,
				NULL, 			// lpEnvironment,
				NULL,			// lpCurrentDirectory,
				&startup_info,	// lpStartupInfo,
				&process_info	// lpProcessInformation
			);

			if (!success)
				goto launch_error;

			// Store the process handle.
			ac_handle->mProcessHandle = process_info.hProcess;

			// Don't need the thread handle, close it now.
			CloseHandle(process_info.hThread);
		}
	}
	
	// Return the handle.
	*ouHandle = ac_handle;
	return 0;

launch_error:
	// Cleanup.
	AssetCooker_Detach(&ac_handle);
	return -1;
}


int AssetCooker_IsAlive(AssetCookerHandle inHandle)
{
	if (inHandle == NULL)
		return 0;

	return WaitForSingleObject(inHandle->mProcessHandle, 0) != WAIT_OBJECT_0;
}


int AssetCooker_Kill(AssetCookerHandle* ioHandlePtr)
{
	if (ioHandlePtr == NULL || *ioHandlePtr == NULL)
		return -1;

	BOOL success = SetEvent((*ioHandlePtr)->mEventKill);

	AssetCooker_Detach(ioHandlePtr);

	return success ? 0 : -1;
}


int AssetCooker_Detach(AssetCookerHandle* ioHandlePtr)
{
	if (ioHandlePtr == NULL || *ioHandlePtr == NULL)
		return -1;

	CloseHandle((*ioHandlePtr)->mEventKill);
	CloseHandle((*ioHandlePtr)->mEventPause);
	CloseHandle((*ioHandlePtr)->mEventUnpause);
	CloseHandle((*ioHandlePtr)->mEventIsPaused);
	CloseHandle((*ioHandlePtr)->mEventIsIdle);
	CloseHandle((*ioHandlePtr)->mProcessHandle);
	
	free(*ioHandlePtr);
	*ioHandlePtr = NULL;

	return 0;
}


int AssetCooker_Pause(AssetCookerHandle inHandle, int inPause)
{
	if (inHandle == NULL)
		return -1;

	BOOL success;
	if (inPause)
		success = SetEvent(inHandle->mEventPause);
	else
		success = SetEvent(inHandle->mEventUnpause);

	return success ? 0 : -1;
}


int AssetCooker_ShowWindow(AssetCookerHandle inHandle)
{
	if (inHandle == NULL)
		return -1;

	BOOL success = SetEvent(inHandle->mEventShowWindow);

	return success ? 0 : -1;
}


int AssetCooker_IsPaused(AssetCookerHandle inHandle)
{
	if (inHandle == NULL)
		return 0;

	return (WaitForSingleObject(inHandle->mEventIsPaused, 0) == WAIT_OBJECT_0);
}


int AssetCooker_HasErrors(AssetCookerHandle inHandle)
{
	if (inHandle == NULL)
		return 0;

	return (WaitForSingleObject(inHandle->mEventHasErrors, 0) == WAIT_OBJECT_0);
}


int AssetCooker_IsIdle(AssetCookerHandle inHandle)
{
	if (inHandle == NULL)
		return 0;

	return (WaitForSingleObject(inHandle->mEventIsIdle, 0) == WAIT_OBJECT_0);
}


int AssetCooker_WaitForIdle(AssetCookerHandle inHandle)
{
	if (inHandle == NULL)
		return -1;

	int success = (WaitForSingleObject(inHandle->mEventIsIdle, INFINITE) == WAIT_OBJECT_0);
	return success ? 0 : -1;
}
