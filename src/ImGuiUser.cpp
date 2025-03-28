/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"

#include <Bedrock/StringFormat.h>


bool ImGui::BeginPopupWithTitle(const char* str_id, StringView inTitle, ImGuiWindowFlags flags)
{
	ImGuiID       id = GetID(str_id);
    ImGuiContext& g = *GImGui;
    if (!IsPopupOpen(id, ImGuiPopupFlags_None))
    {
        g.NextWindowData.ClearFlags(); // We behave like Begin() and need to consume those values
        return false;
    }

    flags |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
    bool is_open = Begin(gTempFormat("%s##%08x", inTitle.AsCStr(), id), NULL, flags);
    if (!is_open) // NB: Begin can return false when the popup is completely clipped (e.g. zero size display)
        EndPopup();

    return is_open;
}
