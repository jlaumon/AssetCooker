#pragma once

#include "Strings.h"
#include "data/fonts/Fork-Awesome/IconsForkAwesome.h"

namespace ImGui
{
bool                                 ButtonGrad(ImStrv label, const ImVec2& size = ImVec2(0, 0));

bool                                 BeginPopupWithTitle(const char* str_id, StringView inTitle, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
}
