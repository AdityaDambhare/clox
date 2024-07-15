#ifndef CLOX_DEBUG_H
#define CLOX_DEBUG_H
#include "chunk.h"

void dissassembleChunk(Chunk* chunk, const char* name);
int dissassembleInstruction(Chunk* chunk, int offset);

#endif