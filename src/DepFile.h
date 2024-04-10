#pragma once
#include "CookingSystem.h"

bool gReadDepFile(DepFileFormat inFormat, FileID inDepFileID, std::vector<FileID>& outInputs, std::vector<FileID>& outOutputs);
void gApplyDepFileContent(CookingCommand& ioCommand, Span<FileID> inDepFileInputs, Span<FileID> inDepFileOutputs);