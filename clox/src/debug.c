#include <stdio.h>
#include "debug.h"
#include "value.h"

void dissassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = dissassembleInstruction(chunk, offset);
  }
}
static int simpleInstruction(const char* name,int offset){
  printf("%s\n",name);
  return offset+1;
}
static int constantInstruction(const char* name,Chunk* chunk,int offset){
  uint8_t constant = chunk->code[offset+1];
  printf("%-16s %4d '",name,constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset+2;
}
static int constantIntsructionLong(const char* name,Chunk* chunk,int offset){
  uint8_t high_byte = chunk->code[offset+1];
  uint8_t low_byte = chunk->code[offset+2];
  uint16_t combined = (high_byte<<8)|low_byte;
  printf("%-16s %4d '",name,combined);
  printValue(chunk->constants.values[combined]);
  printf("'\n");
  return offset+3;
}
int dissassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset);
  int line = getLine(chunk, offset);
  if (offset > 0 && line == getLine(chunk, offset - 1)) {
    printf("   | ");
  } else {
    printf("%4d ", line);
  }

  static void* dispatchTable[] = 
  {&&RETURN,
  &&CONSTANT,
  &&NIL,
  &&TRUE,
  &&FALSE,
  &&EQUAL,
  &&GREATER,
  &&LESS,
  &&CONSTANT_LONG,
  &&ADD,
  &&SUBTRACT,
  &&MULTIPLY,
  &&DIVIDE,
  &&NOT,
  &&NEGATE,
  &&POP,
  &&PRINT,
  &&DEFINE_GLOBAL,
  &&DEFINE_GLOBAL_LONG,
  &&GET_GLOBAL,
  &&GET_GLOBAL_LONG,
  &&SET_GLOBAL,
  &&SET_GLOBAL_LONG,
  };

  uint8_t instruction = chunk->code[offset];
  if (instruction >= 0 && instruction < sizeof(dispatchTable) / sizeof(dispatchTable[0])) {
    goto *dispatchTable[instruction];
  } else {
    printf("Unknown instruction %d\n", instruction);
    return offset + 1; // Or some error handling
  }
  RETURN:
    return simpleInstruction("OP_RETURN",offset);
  CONSTANT:
    return constantInstruction("OP_CONSTANT",chunk,offset);
  NIL:
    return simpleInstruction("OP_NIL",offset);
  TRUE:
    return simpleInstruction("OP_TRUE",offset);
  FALSE:
    return simpleInstruction("OP_FALSE",offset);
  EQUAL:
    return simpleInstruction("OP_EQUAL",offset);
  GREATER:
    return simpleInstruction("OP_GREATER",offset);
  LESS:
    return simpleInstruction("OP_LESS",offset);
  CONSTANT_LONG:
    return constantIntsructionLong("OP_CONSTANT_LONG",chunk,offset);
  ADD:
    return simpleInstruction("OP_ADD",offset);
  SUBTRACT:
    return simpleInstruction("OP_SUBTRACT",offset);
  MULTIPLY:
    return simpleInstruction("OP_MULTIPLY",offset);
  DIVIDE:
    return simpleInstruction("OP_DIVIDE",offset);
  NOT:
    return simpleInstruction("OP_NOT",offset);
  NEGATE:
    return simpleInstruction("OP_NEGATE",offset);
  POP:
    return simpleInstruction("OP_POP",offset);
  PRINT:
    return simpleInstruction("OP_PRINT",offset);
  DEFINE_GLOBAL:
    return constantInstruction("OP_DEFINE_GLOBAL",chunk,offset);
  DEFINE_GLOBAL_LONG:
    return constantIntsructionLong("OP_DEFINE_GLOBAL_LONG",chunk,offset);
  GET_GLOBAL:
    return constantInstruction("OP_GET_GLOBAL",chunk,offset);
  GET_GLOBAL_LONG:
    return constantIntsructionLong("OP_GET_GLOBAL_LONG",chunk,offset);
  SET_GLOBAL:
    return constantInstruction("OP_SET_GLOBAL",chunk,offset);
  SET_GLOBAL_LONG:
    return constantIntsructionLong("OP_SET_GLOBAL_LONG",chunk,offset);
}