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

	template<typename... taArgs> void Add(std::format_string<taArgs...> inFmt, const taArgs&... inArgs)
	{
		Add(LogType::Normal, inFmt, std::make_format_args(inArgs...));
	}

	template<typename... taArgs> void AddError(std::format_string<taArgs...> inFmt, const taArgs&... inArgs)
	{
		Add(LogType::Error, inFmt, std::make_format_args(inArgs...));
	}

	// Returns the formatted string out of convenience.
	StringView                      Add(LogType inType, std::string_view inFmt, std::format_args inArgs);

	void Clear();
	void Draw(StringView inName, bool* ioOpen = nullptr);

	StringPool::ResizableStringView	StartLine(LogType inType);
	void							FinishLine(StringPool::ResizableStringView& inLine);
	
	std::vector<StringView> mLines;
	StringPool              mStringPool;
	ImGuiTextFilter         mFilter;
};