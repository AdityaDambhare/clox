#include <stdio.h>
#include "debug.h"
#include "value.h"
#include "object.h"
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
static int constantInstructionLong(const char* name,Chunk* chunk,int offset){
  uint8_t high_byte = chunk->code[offset+1];
  uint8_t low_byte = chunk->code[offset+2];
  uint16_t combined = (high_byte<<8)|low_byte;
  printf("%-16s %4d '",name,combined);
  if(combined<chunk->constants.count){
  printValue(chunk->constants.values[combined]);
  }
  else{
    printValue(NIL_VAL);
  }
  printf("'\n");
  return offset+3;
}

static int jumpInstruction(const char* name, int sign,
                           Chunk* chunk, int offset) {
  uint16_t jump = (uint16_t)(chunk->code[offset + 1] << 8);
  jump |= chunk->code[offset + 2];
  printf("%-16s %4d -> %d\n", name, offset,
         offset + 3 + sign * jump);
  return offset + 3;
}

static int byteInstruction(const char* name, Chunk* chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  printf("%-16s %4d\n", name, slot);
  return offset + 2;
}

static int byteInstructionLong(const char* name, Chunk* chunk, int offset) {
  uint8_t high_byte = chunk->code[offset + 1];
  uint8_t low_byte = chunk->code[offset + 2];
  uint16_t combined = (high_byte << 8) | low_byte;
  printf("%-16s %4d\n", name, combined);
  return offset + 3;
}

int dissassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset);
  int line = getLine(chunk, offset);
  if (offset > 0 && line == getLine(chunk, offset - 1)) {
    printf("   | ");
  } else {
    printf("%4d ", line);
  }
  //i really regret using computed gotos here. what was i thinking. 
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
  &&POWER,
  &&POP,
  &&PRINT,
  &&DEFINE_GLOBAL,
  &&DEFINE_GLOBAL_LONG,
  &&GET_GLOBAL,
  &&GET_GLOBAL_LONG,
  &&SET_GLOBAL,
  &&SET_GLOBAL_LONG,
  &&GET_LOCAL,
  &&SET_LOCAL,
  &&JUMP,
  &&JUMP_IF_FALSE,
  &&LOOP,
  &&CALL,
  &&CLOSURE,
  &&GET_UP,
  &&SET_UP,
  &&CLOSE_UP,
  &&CLASS,
  &&GET_MEM,
  &&SET_MEM,
  &&METHOD,
  &&INVOKE
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
    return constantInstructionLong("OP_CONSTANT_LONG",chunk,offset);
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
  POWER:
    return simpleInstruction("OP_POWER",offset);
  POP:
    return simpleInstruction("OP_POP",offset);
  PRINT:
    return simpleInstruction("OP_PRINT",offset);
  DEFINE_GLOBAL:
    return constantInstruction("OP_DEFINE_GLOBAL",chunk,offset);
  DEFINE_GLOBAL_LONG:
    return constantInstructionLong("OP_DEFINE_GLOBAL_LONG",chunk,offset);
  GET_GLOBAL:
    return constantInstruction("OP_GET_GLOBAL",chunk,offset);
  GET_GLOBAL_LONG:
    return constantInstructionLong("OP_GET_GLOBAL_LONG",chunk,offset);
  SET_GLOBAL:
    return constantInstruction("OP_SET_GLOBAL",chunk,offset);
  SET_GLOBAL_LONG:
    return constantInstructionLong("OP_SET_GLOBAL_LONG",chunk,offset);
  GET_LOCAL:
    return constantInstructionLong("OP_GET_LOCAL",chunk,offset);
  SET_LOCAL:
    return constantInstructionLong("OP_SET_LOCAL",chunk,offset);
  JUMP:
    return jumpInstruction("OP_JUMP", 1, chunk, offset);
  JUMP_IF_FALSE:
    return jumpInstruction("OP_JUMP_IF_FALSE", 1, chunk, offset);
  LOOP:
    return jumpInstruction("OP_LOOP", -1, chunk, offset);
  CALL:
    return byteInstruction("OP_CALL", chunk, offset);
  CLOSURE:
    { offset++;
      int constant = chunk->code[offset++]<<8|chunk->code[offset++];
      printf("%-16s %4d ", "OP_CLOSURE", constant);
      printValue(chunk->constants.values[constant]);
      printf("\n");
      ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++]<<8|chunk->code[offset++];
        printf("%04d      |                     %s %d\n",
               offset - 2, isLocal ? "local" : "upvalue", index);
      }
      return offset;    
    }
  GET_UP:
    return byteInstructionLong("OP_GET_UPVALUE", chunk, offset);
  SET_UP:
    return byteInstructionLong("OP_SET_UPVALUE", chunk, offset);
  CLOSE_UP:
    return simpleInstruction("OP_CLOSE_UPVALUE", offset);
  CLASS:
    return constantInstructionLong("OP_CLASS", chunk, offset);
  GET_MEM:
    return constantInstructionLong("OP_GET_PROPERTY", chunk, offset);
  SET_MEM:
    return constantInstructionLong("OP_SET_PROPERTY", chunk, offset);
  METHOD:
    return constantInstructionLong("OP_METHOD", chunk, offset);
  INVOKE:
    {
      offset++;
      int constant = chunk->code[offset++]<<8|chunk->code[offset++];
      int argCount = chunk->code[offset++];
      printf("%-16s (%d args) %4d '", "OP_INVOKE", argCount, constant);
      printValue(chunk->constants.values[constant]);
      printf("'\n");
      return offset;
    }
}