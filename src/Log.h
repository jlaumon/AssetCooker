/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "StringPool.h"
#include "imgui.h"

#include <Bedrock/Mutex.h>

enum class LogType : uint8
{
	Normal,
	Error
};

struct Log
{
	static constexpr StringView  cErrorTag = "[error]";

	// Returns the formatted string out of convenience.
	StringView                      Add(LogType inType, StringView inFormat, va_list inArgs);

	void Clear();
	void Draw(StringView inName, bool* ioOpen = nullptr);

	StringPool::ResizableStringView	StartLine(LogType inType);
	void							FinishLine(LogType inType, StringPool::ResizableStringView& inLine);

	struct Line
	{
		const char* mData = nullptr;
		int         mSize = 0;
		LogType     mType = LogType::Normal;
		StringView  AsStringView() const { return { mData, mSize }; }
	};
	
	SegmentedVector<Line>   mLines;
	StringPool              mStringPool;
	Mutex                   mMutex;
	ImGuiTextFilter         mFilter;
	bool                    mAutoAddErrorTag = false;
	bool                    mAutoAddTime     = false;
};