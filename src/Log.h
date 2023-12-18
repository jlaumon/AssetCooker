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

	void Add(StringView inString, LogType inType = LogType::Normal);
	void Clear();
	void Draw();
	
	std::vector<StringView> mLines;
	StringPool              mStringPool;
	ImGuiTextFilter         mFilter;
};