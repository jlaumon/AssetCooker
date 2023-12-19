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

	// TODO: Support formatting directly here. Use std::vformat_to and make StringPool support std::back_inserter

	void Add(StringView inString, LogType inType = LogType::Normal);
	void AddError(StringView inString) { Add(inString, LogType::Error); }
	void Clear();
	void Draw();
	
	std::vector<StringView> mLines;
	StringPool              mStringPool;
	ImGuiTextFilter         mFilter;
};