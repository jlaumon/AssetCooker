#pragma once

#include "Strings.h"
#include "data/fonts/Fork-Awesome/IconsForkAwesome.h"

namespace ImGui
{
inline void                          Text(StringView inText) { ImGui::TextUnformatted(inText.data(), gEndPtr(inText)); }
inline void                          TextUnformatted(StringView inText) { ImGui::TextUnformatted(inText.data(), gEndPtr(inText)); }

template <size_t taSize> inline void Text(const TempString<taSize>& inTempString) { ImGui::TextUnformatted(inTempString.AsStringView()); }
template <size_t taSize> inline void TextUnformatted(const TempString<taSize>& inTempString) { ImGui::TextUnformatted(inTempString.AsStringView()); }
template <size_t taSize> inline void SeparatorText(const TempString<taSize>& inTempString) { ImGui::SeparatorText(inTempString.AsCStr()); }

bool                                 ColoredButtonV1(const char* label, const ImVec2& size, ImU32 text_color, ImU32 bg_color_1, ImU32 bg_color_2);
bool                                 ButtonGrad(const char* label, const ImVec2& size = ImVec2(0, 0));

bool                                 BeginPopupWithTitle(const char* str_id, StringView inTitle, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
}
