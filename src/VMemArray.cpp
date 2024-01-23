#include "VMemArray.h"
#include "App.h"

#include "Debug.h"
#include "win32/file.h"
#include "win32/io.h"
#include "win32/sysinfo.h"

size_t gVMemReserveGranularity()
{
    struct Initializer
    {
		Initializer()
		{
			SYSTEM_INFO sys_info;
		    GetSystemInfo(&sys_info);
		    mGranularity = sys_info.dwAllocationGranularity;
		}

		size_t mGranularity;
    };

	static Initializer init;
	return init.mGranularity;
}


size_t gVMemCommitGranularity()
{
    struct Initializer
    {
		Initializer()
		{
			SYSTEM_INFO sys_info;
		    GetSystemInfo(&sys_info);
		    mGranularity = sys_info.dwPageSize;
		}

		size_t mGranularity;
    };

	static Initializer init;
	return init.mGranularity;
}


VMemBlock gVMemReserve(size_t inSize)
{
	inSize = gAlignUp(inSize, gVMemReserveGranularity());
	void* ptr = VirtualAlloc(nullptr, inSize, MEM_RESERVE, PAGE_NOACCESS);

	if (ptr == nullptr)
		gApp.FatalError("VirtualAlloc failed - {}", GetLastErrorString());

	return { (uint8*)ptr, (uint8*)ptr + inSize };
}


void gVMemFree(VMemBlock inBlock)
{
	if (!VirtualFree(inBlock.mBegin, 0, MEM_RELEASE))
		gApp.FatalError("VirtualFree failed - {}", GetLastErrorString());
}


VMemBlock gVMemCommit(VMemBlock inBlock)
{
	inBlock.mBegin = (uint8*)gAlignDown((uintptr_t)inBlock.mBegin, gVMemCommitGranularity());
	inBlock.mEnd   = (uint8*)gAlignUp((uintptr_t)inBlock.mEnd, gVMemCommitGranularity());

	if (!VirtualAlloc(inBlock.mBegin, inBlock.Size(), MEM_COMMIT, PAGE_READWRITE))
		gApp.FatalError("VirtualAlloc failed - {}", GetLastErrorString());

	return inBlock;
}