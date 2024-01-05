#pragma once

#include "Strings.h"

namespace ImGui
{
inline void TextUnformatted(StringView inText) { ImGui::TextUnformatted(inText.data(), gEndPtr(inText)); }
}
