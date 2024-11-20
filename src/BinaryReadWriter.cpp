/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "BinaryReadWriter.h"
#include "lz4.h"
#include <stdio.h>

bool BinaryWriter::WriteFile(FILE* ioFile)
{
	// Compress the data with LZ4.
	// LZ4HC gives a slightly better ratio but is 10 times as slow, so not worth it here.
	int    uncompressed_size   = (int)mBuffer.SizeRelaxed();
	int    compressed_size_max = LZ4_compressBound(uncompressed_size);
	char*  compressed_buffer   = (char*)malloc(compressed_size_max);
	int    compressed_size     = LZ4_compress_default((const char*)mBuffer.Begin(), compressed_buffer, uncompressed_size, compressed_size_max);

	defer { free(compressed_buffer); };

	// Write the uncompressed size first, we'll need it to decompress.
	if (fwrite(&uncompressed_size, sizeof(uncompressed_size), 1, ioFile) != 1)
		return false;

	// Write the compressed data.
	int written_size = fwrite(compressed_buffer, 1, compressed_size, ioFile);
	return written_size == compressed_size;
}


bool BinaryReader::ReadFile(FILE* inFile)
{
	if (fseek(inFile, 0, SEEK_END) != 0)
		return false;

	int file_size = ftell(inFile);
	if (file_size == -1)
		return false;

	if (fseek(inFile, 0, SEEK_SET) != 0)
		return false;

	// First read the uncompressed size.
	int uncompressed_size = 0;
	if (fread(&uncompressed_size, sizeof(uncompressed_size), 1, inFile) != 1)
		return false;

	// Then read the compressed data.
	int   compressed_size   = file_size - (int)sizeof(uncompressed_size);
	char* compressed_buffer = (char*)malloc(compressed_size);
	defer { free(compressed_buffer); };

	if (fread(compressed_buffer, 1, compressed_size, inFile) != compressed_size)
		return false;

	Span uncompressed_buffer = mBuffer.EnsureCapacity(uncompressed_size, mBufferLock);

	// Decompress the data.
	int actual_uncompressed_size = LZ4_decompress_safe(compressed_buffer, (char*)uncompressed_buffer.Data(), compressed_size, uncompressed_buffer.Size());
	gAssert(actual_uncompressed_size == uncompressed_size);

	mBuffer.IncreaseSize(uncompressed_size, mBufferLock);

	return true;
}