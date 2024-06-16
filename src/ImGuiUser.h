#pragma once

#include "Strings.h"
#include "data/fonts/Fork-Awesome/IconsForkAwesome.h"

namespace ImGui
{
bool                                 BeginPopupWithTitle(const char* str_id, StringView inTitle, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
}
