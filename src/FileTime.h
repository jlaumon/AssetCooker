#pragma once
#include "Core.h"

// Forward declarations.
struct FileTime;
struct SystemTime;
struct LocalTime;

// Forward declarations of Win32 types.
struct _FILETIME;
struct _SYSTEMTIME;


// Alias for FILETIME.
struct FileTime
{
	uint64 mDateTime                                              = 0;

	constexpr FileTime()                                          = default;
	constexpr FileTime(const FileTime&)                           = default;
	constexpr ~FileTime()                                         = default;
	constexpr FileTime& operator=(const FileTime&)                = default;
	constexpr bool      operator==(const FileTime& inOther) const = default;

	// Conversion to/from FILETIME.
	FileTime(const _FILETIME& inFileTime) { *this = inFileTime; }
	FileTime(uint64 inTime) { mDateTime = inTime; }
	_FILETIME ToWin32() const;
	FileTime& operator=(const _FILETIME&);
	FileTime& operator=(uint64 inTime)
	{
		mDateTime = inTime;
		return *this;
	}

	int64                     operator-(FileTime inOther) const { return ((int64)mDateTime - (int64)inOther.mDateTime) * 100; } // Difference in nano seconds.

	SystemTime                ToSystemTime() const;
	LocalTime                 ToLocalTime() const;

	constexpr bool            IsValid() const { return *this != cInvalid(); }
	static constexpr FileTime cInvalid() { return {}; }
};
static_assert(sizeof(FileTime) == 8);


// Alias for SYSTEMTIME.
struct SystemTime
{
	uint16 mYear                                                      = 0;
	uint16 mMonth                                                     = 0;
	uint16 mDayOfWeek                                                 = 0;
	uint16 mDay                                                       = 0;
	uint16 mHour                                                      = 0;
	uint16 mMinute                                                    = 0;
	uint16 mSecond                                                    = 0;
	uint16 mMilliseconds                                              = 0;

	constexpr SystemTime()                                            = default;
	constexpr SystemTime(const SystemTime&)                           = default;
	constexpr ~SystemTime()                                           = default;
	constexpr SystemTime& operator=(const SystemTime&)                = default;
	constexpr bool        operator==(const SystemTime& inOther) const = default;

	// Conversion to/from SYSTEMTIME.
	SystemTime(const _SYSTEMTIME& inSystemTime) { *this = inSystemTime; }
	_SYSTEMTIME                 ToWin32() const;
	SystemTime&                 operator=(const _SYSTEMTIME&);

	FileTime                    ToFileTime() const;
	LocalTime                   ToLocalTime() const;

	constexpr bool              IsValid() const { return *this != cInvalid(); }
	static constexpr SystemTime cInvalid() { return {}; }
};
static_assert(sizeof(SystemTime) == 16);


// Version of SystemTime dedicated to storing the local time.
// That's the version that should be used for display, not to convert back to anything else.
struct LocalTime
{
	uint16 mYear                                                          = 0;
	uint16 mMonth                                                         = 0;
	uint16 mDayOfWeek                                                     = 0;
	uint16 mDay                                                           = 0;
	uint16 mHour                                                          = 0;
	uint16 mMinute                                                        = 0;
	uint16 mSecond                                                        = 0;
	uint16 mMilliseconds                                                  = 0;

	constexpr LocalTime()                                                 = default;
	constexpr LocalTime(const LocalTime&)                                 = default;
	constexpr ~LocalTime()                                                = default;
	constexpr LocalTime&       operator=(const LocalTime&)                = default;
	constexpr bool             operator==(const LocalTime& inOther) const = default;

	constexpr bool             IsValid() const { return *this != cInvalid(); }
	static constexpr LocalTime cInvalid() { return {}; }
};
static_assert(sizeof(LocalTime) == 16);


SystemTime gGetSystemTime();
LocalTime  gGetLocalTime();
FileTime   gGetSystemTimeAsFileTime();
