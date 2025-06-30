// SPDX-License-Identifier: Unlicense
#include "asset_cooker_api.h"

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

enum EventResetMode
{
	Event_ManualReset,
	Event_AutoReset,
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


struct asset_cooker_s
{
	HANDLE mProcessHandle;

	HANDLE mEventKill;
	HANDLE mEventPause;
	HANDLE mEventUnpause;
	HANDLE mEventShowWindow;
	HANDLE mEventIsPaused;
	HANDLE mEventIsIdle;
	HANDLE mEventHasErrors;
};
typedef struct asset_cooker_s asset_cooker_s;


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


HANDLE sCreateSharedEvent(const char* inAssetCookerIdentifier, const char* inEventName, enum EventResetMode inResetMode)
{
	char event_name[MAX_PATH];
	sStrConcat(inAssetCookerIdentifier, inEventName, STR_OUT(event_name));

	return CreateEventA(NULL, inResetMode == Event_ManualReset, FALSE, event_name);
}


int asset_cooker_launch(const char* exe_path, const char* config_file_path, int options, asset_cooker_handle* out_handle)
{
	// Make sure the config file exists.
	if (!sFileExists(config_file_path))
		return -1;

	// Get the asset cooker identifier.
	char asset_cooker_id[MAX_PATH];
	if (sGetAssetCookerIdentifier(config_file_path, STR_OUT(asset_cooker_id)) < 0)
		return -1;

	HANDLE process_handle = NULL;

	// Try to open the shared memory.
	{
		char shared_memory_name[MAX_PATH];
		if (!sStrConcat(asset_cooker_id, " SharedMemory", STR_OUT(shared_memory_name)))
			return -1;

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
				return -1;
			}

			// Get the process handle.
			DWORD process_id = shared_memory_ptr->mProcessID;

			// Unmap & close the shared memory.
			UnmapViewOfFile(shared_memory_ptr);
			CloseHandle(shared_memory_handle);

			// Get a handle to Asset Cooker's process.
			process_handle = OpenProcess(SYNCHRONIZE, FALSE, process_id);
			if (process_handle == NULL)
				return -1;
		}
		else
		{
			// Shared memory does not exist, Asset Cooker is probably not running.
			// Launch it now.
			char command_line[MAX_PATH + 32];
			if (!sStrConcat("-config_file ", config_file_path, STR_OUT(command_line)))
				return -1;

			PROCESS_INFORMATION process_info;
			STARTUPINFOA startup_info;
			memset(&startup_info, 0, sizeof(startup_info));
			startup_info.cb = sizeof(startup_info);

			if (options & assetcooker_option_start_minimized)
			{
				startup_info.dwFlags |= STARTF_USESHOWWINDOW;
				startup_info.wShowWindow = SW_SHOWMINIMIZED;
			}

			BOOL success = CreateProcessA(
				exe_path,
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
				return -1;

			process_handle = process_info.hProcess;

			// Don't need the thread handle, close it now.
			CloseHandle(process_info.hThread);
		}

	}
	
	// Allocate a handle.
	asset_cooker_s* ac_handle = calloc(1, sizeof(asset_cooker_s));
	*out_handle = calloc(1, sizeof(asset_cooker_s));
	ac_handle->mProcessHandle = process_handle;

	// Open the shared events.
	ac_handle->mEventKill		= sCreateSharedEvent(asset_cooker_id, " Kill", Event_AutoReset);
	ac_handle->mEventPause		= sCreateSharedEvent(asset_cooker_id, " Pause", Event_AutoReset);
	ac_handle->mEventUnpause	= sCreateSharedEvent(asset_cooker_id, " Unpause", Event_AutoReset);
	ac_handle->mEventShowWindow	= sCreateSharedEvent(asset_cooker_id, " ShowWindow", Event_AutoReset);
	ac_handle->mEventIsPaused	= sCreateSharedEvent(asset_cooker_id, " IsPaused", Event_ManualReset);
	ac_handle->mEventIsIdle		= sCreateSharedEvent(asset_cooker_id, " IsIdle", Event_ManualReset);
	ac_handle->mEventHasErrors	= sCreateSharedEvent(asset_cooker_id, " HasErrors", Event_ManualReset);

	if (ac_handle->mEventKill		== NULL ||
		ac_handle->mEventPause		== NULL ||
		ac_handle->mEventUnpause	== NULL ||
		ac_handle->mEventShowWindow == NULL ||
		ac_handle->mEventIsPaused	== NULL ||
		ac_handle->mEventIsIdle		== NULL ||
		ac_handle->mEventHasErrors	== NULL)
	{
		// Clean up.
		asset_cooker_detach(&ac_handle);
		return -1;
	}

	// Return the handle.
	*out_handle = ac_handle;
	return 0;

}


int asset_cooker_is_alive(asset_cooker_handle handle)
{
	if (handle == NULL)
		return 0;

	return WaitForSingleObject(handle->mProcessHandle, 0) != WAIT_OBJECT_0;
}


int asset_cooker_kill(asset_cooker_handle* handle_ptr)
{
	if (handle_ptr == NULL || *handle_ptr == NULL)
		return 0;

	BOOL success = SetEvent((*handle_ptr)->mEventKill);

	asset_cooker_detach(handle_ptr);

	return success ? 0 : -1;
}


int asset_cooker_detach(asset_cooker_handle* handle_ptr)
{
	if (handle_ptr == NULL || *handle_ptr == NULL)
		return -1;

	CloseHandle((*handle_ptr)->mEventKill);
	CloseHandle((*handle_ptr)->mEventPause);
	CloseHandle((*handle_ptr)->mEventUnpause);
	CloseHandle((*handle_ptr)->mEventIsPaused);
	CloseHandle((*handle_ptr)->mEventIsIdle);
	CloseHandle((*handle_ptr)->mProcessHandle);
	
	free(*handle_ptr);
	*handle_ptr = NULL;

	return 0;
}


int asset_cooker_pause(asset_cooker_handle handle, int pause)
{
	if (handle == NULL)
		return -1;

	BOOL success;
	if (pause)
		success = SetEvent(handle->mEventPause);
	else
		success = SetEvent(handle->mEventUnpause);

	return success ? 0 : -1;
}


int asset_cooker_show_window(asset_cooker_handle handle)
{
	if (handle == NULL)
		return -1;

	BOOL success = SetEvent(handle->mEventShowWindow);

	return success ? 0 : -1;
}


int asset_cooker_is_paused(asset_cooker_handle handle)
{
	if (handle == NULL)
		return 0;

	return (WaitForSingleObject(handle->mEventIsPaused, 0) == WAIT_OBJECT_0);
}


int asset_cooker_has_errors(asset_cooker_handle handle)
{
	if (handle == NULL)
		return 0;

	return (WaitForSingleObject(handle->mEventHasErrors, 0) == WAIT_OBJECT_0);
}


int asset_cooker_is_idle(asset_cooker_handle handle)
{
	if (handle == NULL)
		return 0;

	return (WaitForSingleObject(handle->mEventIsIdle, 0) == WAIT_OBJECT_0);
}


int asset_cooker_wait_for_idle(asset_cooker_handle handle)
{
	if (handle == NULL)
		return -1;

	int success = (WaitForSingleObject(handle->mEventIsIdle, INFINITE) == WAIT_OBJECT_0);
	return success ? 0 : -1;
}
