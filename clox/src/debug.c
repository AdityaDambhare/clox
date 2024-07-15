#include <stdio.h>
#include "debug.h"
#include "value.h"

void dissassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = dissassembleInstruction(chunk, offset);
  }
}

int dissassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset);
  int line = getLine(chunk, offset);
  if (offset > 0 && line == getLine(chunk, offset - 1)) {
    printf("   | ");
  } else {
    printf("%4d ", line);
  }
  static void* dispatchTable[3] = {&&RETURN,&&CONSTANT,&&CONSTANT_LONG};
  uint8_t instruction = chunk->code[offset];
  if (instruction >= 0 && instruction < sizeof(dispatchTable) / sizeof(dispatchTable[0])) {
    goto *dispatchTable[instruction];
  } else {
    printf("Unknown instruction %d\n", instruction);
    return offset + 1; // Or some error handling
  }
  RETURN:
    printf("OP_RETURN\n");
    return offset + 1;
  CONSTANT:
    printf("%-16s %4d '", "OP_CONSTANT", instruction);
    instruction = chunk->code[offset+1];
    printValue(chunk->constants.values[instruction]);
    printf("'\n");
    return offset + 2;
  CONSTANT_LONG:
    printf("%-16s %4d'", "OP_CONSTANT_LONG", instruction);
    uint8_t high_byte = chunk->code[offset+1];
    uint8_t low_byte = chunk->code[offset+2];
    uint16_t combined = (high_byte<<8) | low_byte;
    printValue(chunk->constants.values[combined]);
    printf("'\n");
    return offset + 3;
}