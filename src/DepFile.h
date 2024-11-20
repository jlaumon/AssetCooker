/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#pragma once
#include "CookingSystem.h"

bool gReadDepFile(DepFileFormat inFormat, FileID inDepFileID, Vector<FileID>& outInputs, Vector<FileID>& outOutputs);
void gApplyDepFileContent(CookingCommand& ioCommand, Span<FileID> inDepFileInputs, Span<FileID> inDepFileOutputs);