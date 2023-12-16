#include "FileSystem.h"
#include "App.h"
#include "Debug.h"

#include "win32/misc.h"
#include "win32/file.h"
#include "win32/io.h"

#include <format>



void FileRepo::Init(std::string_view inRootPath)
{
	mRootPath         = mStringPool.AllocateCopy(inRootPath);
	char drive_letter = mRootPath[0];

	// Get a handle to the drive.
	// Note: Only request FILE_TRAVERSE to make that work without admin rights.
	HANDLE drive_handle = CreateFileA(std::format(R"(\\.\{}:)", drive_letter).c_str(), (DWORD)FILE_TRAVERSE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (drive_handle == INVALID_HANDLE_VALUE)
		gApp.FatalError(std::format(R"(Failed to get handle to {}:\ - )", drive_letter) + GetLastErrorString());

	USN_JOURNAL_DATA_V0 journal_data;
	uint32				available_bytes;
	if (!DeviceIoControl(drive_handle, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &journal_data, sizeof(journal_data), &available_bytes, nullptr))
		gApp.FatalError(std::format(R"(Failed to get USN journal for {}:\ - )", drive_letter) + GetLastErrorString());


}
