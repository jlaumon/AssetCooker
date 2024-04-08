#pragma once
#include "CookingSystem.h"

bool gReadDepFile(DepFileFormat inFormat, FileID inDepFileID, std::vector<FileID>& outInputs, std::vector<FileID>& outOutputs);