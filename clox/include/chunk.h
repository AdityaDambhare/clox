#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H
#include "common.h"
#include "value.h"
typedef enum {
  OP_RETURN,
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_CONSTANT_LONG,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_POWER,
  OP_POP,
  OP_PRINT,
  OP_DEFINE_GLOBAL,
  OP_DEFINE_GLOBAL_LONG,
  OP_GET_GLOBAL,
  OP_GET_GLOBAL_LONG,
  OP_SET_GLOBAL,
  OP_SET_GLOBAL_LONG,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL
} OpCode;

typedef struct{
  int offset;
  int line;
}LineStart;

typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  LineStart* lines;
  ValueArray constants;
  int lineCount;
  int lineCapacity;
} Chunk;

int getLine(Chunk* chunk,int offset);

void initChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte,int line);
void freeChunk(Chunk* chunk);

int addConstant(Chunk* chunk, Value value);
bool writeConstant(Chunk* chunk,Value value,int line);
#endif