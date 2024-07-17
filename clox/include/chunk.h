#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H
#include "common.h"
#include "value.h"
typedef enum {
  OP_RETURN,
  OP_CONSTANT,
  OP_CONSTANT_LONG,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NEGATE,
  OP_POP
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