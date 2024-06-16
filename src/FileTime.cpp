/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "FileTime.h"


#include "win32/file.h"
#include "win32/misc.h"



_FILETIME FileTime::ToWin32() const
{
	static_assert(sizeof(FileTime) == sizeof(_FILETIME));

	_FILETIME file_time;
	memcpy(&file_time, this, sizeof(*this));
	return file_time;
}


FileTime& FileTime::operator=(const _FILETIME& inFileTime)
{
	static_assert(sizeof(FileTime) == sizeof(_FILETIME));

	memcpy(this, &inFileTime, sizeof(FileTime));
	return *this;
}


SystemTime FileTime::ToSystemTime() const
{
	const FILETIME ft = ToWin32();
	SYSTEMTIME     st = {};
	FileTimeToSystemTime(&ft, &st);
	return st;
}


LocalTime FileTime::ToLocalTime() const
{
	return ToSystemTime().ToLocalTime();
}


_SYSTEMTIME SystemTime::ToWin32() const
{
	static_assert(sizeof(SystemTime) == sizeof(_SYSTEMTIME));

	_SYSTEMTIME system_time;
	memcpy(&system_time, this, sizeof(*this));
	return system_time;
}


SystemTime& SystemTime::operator=(const _SYSTEMTIME& inSystemTime)
{
	static_assert(sizeof(SystemTime) == sizeof(_SYSTEMTIME));

	memcpy(this, &inSystemTime, sizeof(*this));
	return *this;
}


FileTime SystemTime::ToFileTime() const
{
	const SYSTEMTIME st = ToWin32();
	FILETIME         ft = {};
	SystemTimeToFileTime(&st, &ft);
	return ft;
}


LocalTime SystemTime::ToLocalTime() const
{
	const SYSTEMTIME st = ToWin32();
	SYSTEMTIME       local_st = {};
	SystemTimeToTzSpecificLocalTime(nullptr, &st, &local_st);

	static_assert(sizeof(LocalTime) == sizeof(_SYSTEMTIME));

	LocalTime local_time;
	memcpy(&local_time, &local_st, sizeof(local_time));
	return local_time;
}


SystemTime gGetSystemTime()
{
	SYSTEMTIME st = {};
	GetSystemTime(&st);
	return st;
}


LocalTime  gGetLocalTime()
{
	return gGetSystemTime().ToLocalTime();
}


FileTime gGetSystemTimeAsFileTime()
{
	FILETIME ft = {};
	GetSystemTimeAsFileTime(&ft);
	return ft;
}
