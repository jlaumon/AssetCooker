/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include <Bedrock/Core.h>
#include <Bedrock/StringView.h>

void gRemoteControlInit(StringView inAssetCookerID);
void gRemoteControlExit();

void gRemoteControlOnIsPausedChange(bool inIsPaused);
void gRemoteControlOnIsIdleChange(bool inIsIdle);
void gRemoteControlOnHasErrorsChange(bool inHasErrors);

