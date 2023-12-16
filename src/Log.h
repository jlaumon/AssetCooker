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
	static constexpr std::string_view cErrorTag = "[error]";

	void Add(std::string_view inString, LogType inType = LogType::Normal);
	void Clear();
	void Draw();
	
	std::vector<std::string_view> mLines;
	StringPool                    mStringPool;
	ImGuiTextFilter               mFilter;
};