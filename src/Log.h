#pragma once
#include "StringPool.h"
#include "imgui.h"

enum class LogType : uint8
{
	Normal,
	Error
};

struct Log
{
	static constexpr StringView  cErrorTag = "[error]";

	template<typename... taArgs> void Add(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs)
	{
		Add(LogType::Normal, inFmt, fmt::make_format_args(inArgs...));
	}

	template<typename... taArgs> void AddError(fmt::format_string<taArgs...> inFmt, const taArgs&... inArgs)
	{
		Add(LogType::Error, inFmt, fmt::make_format_args(inArgs...));
	}

	// Returns the formatted string out of convenience.
	StringView                      Add(LogType inType, StringView inFmt, fmt::format_args inArgs);

	void Clear();
	void Draw(StringView inName, bool* ioOpen = nullptr);

	StringPool::ResizableStringView	StartLine(LogType inType);
	void							FinishLine(LogType inType, StringPool::ResizableStringView& inLine);

	struct Line
	{
		const char* mData = nullptr;
		int         mSize = 0;
		LogType     mType = LogType::Normal;
		StringView  AsStringView() const { return { mData, (size_t)mSize }; }
	};
	
	SegmentedVector<Line>   mLines;
	StringPool              mStringPool;
	std::mutex              mMutex;
	ImGuiTextFilter         mFilter;
	bool                    mAutoAddErrorTag = false;
	bool                    mAutoAddTime     = false;
};