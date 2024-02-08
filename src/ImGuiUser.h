#pragma once

#include "Strings.h"
#include "data/fonts/Fork-Awesome/IconsForkAwesome.h"

namespace ImGui
{
inline void                          Text(StringView inText) { ImGui::TextUnformatted(inText.data(), gEndPtr(inText)); }
inline void                          TextUnformatted(StringView inText) { ImGui::TextUnformatted(inText.data(), gEndPtr(inText)); }
inline ImVec2                        CalcTextSize(StringView inText, bool hide_text_after_double_hash = false, float wrap_width = -1.0f)
{
	return ImGui::CalcTextSize(inText.data(), gEndPtr(inText), hide_text_after_double_hash, wrap_width);
}

template <size_t taSize> inline void Text(const TempString<taSize>& inTempString) { ImGui::TextUnformatted(inTempString.AsStringView()); }
template <size_t taSize> inline void TextUnformatted(const TempString<taSize>& inTempString) { ImGui::TextUnformatted(inTempString.AsStringView()); }
template <size_t taSize> inline void SeparatorText(const TempString<taSize>& inTempString) { ImGui::SeparatorText(inTempString.AsCStr()); }
template <size_t taSize> inline ImVec2 CalcTextSize(const TempString<taSize>& inTempString, bool hide_text_after_double_hash = false, float wrap_width = -1.0f)
{
	return ImGui::CalcTextSize(inTempString.AsStringView(), hide_text_after_double_hash, wrap_width);
}


bool                                 ColoredButtonV1(const char* label, const ImVec2& size, ImU32 text_color, ImU32 bg_color_1, ImU32 bg_color_2);
bool                                 ButtonGrad(const char* label, const ImVec2& size = ImVec2(0, 0));

bool                                 BeginPopupWithTitle(const char* str_id, StringView inTitle, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
}
