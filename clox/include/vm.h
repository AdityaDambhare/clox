#ifndef CLOX_VM_H
#define CLOX_VM_H
#define FRAMES_MAX 256
#define STACK_MAX (FRAMES_MAX * UINT8_MAX)
#include "chunk.h"
#include "value.h"
#include "table.h"
#include "object.h"

typedef struct {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
} CallFrame;

typedef struct{
    Chunk* chunk;
    uint8_t* ip;
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    Value stack[STACK_MAX];
    Value* stackTop;
    Obj* objects;
    Table strings;
    Table globals;
    ObjUpvalue* openUpvalues;
}VM;

typedef enum{
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
}InterpretResult;

extern VM vm;
void initVM();
void freeVM();
InterpretResult interpret(const char* source);

void push(Value value);
Value pop();

#endif