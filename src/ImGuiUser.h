#pragma once

#include "Strings.h"
#include "data/fonts/vscode-codicons/IconsCodicons.h"

namespace ImGui
{
inline void Text(StringView inText) { ImGui::TextUnformatted(inText.data(), gEndPtr(inText)); }
inline void TextUnformatted(StringView inText) { ImGui::TextUnformatted(inText.data(), gEndPtr(inText)); }

template <size_t taSize> inline void Text(const TempString<taSize>& inTempString) { ImGui::TextUnformatted(inTempString.AsStringView()); }
template <size_t taSize> inline void TextUnformatted(const TempString<taSize>& inTempString) { ImGui::TextUnformatted(inTempString.AsStringView()); }
template <size_t taSize> inline void SeparatorText(const TempString<taSize>& inTempString) { ImGui::SeparatorText(inTempString.AsCStr()); }
}
