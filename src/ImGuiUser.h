/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "Strings.h"
#include "data/fonts/Fork-Awesome/IconsForkAwesome.h"

namespace ImGui
{
bool BeginPopupWithTitle(const char* str_id, StringView inTitle, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
}
