#ifndef CLOX_VM_H
#define CLOX_VM_H
#define FRAMES_MAX 1024
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
    size_t bytesAllocated;
    size_t nextGC;
    Obj* objects;
    Table strings;
    ObjString* initString;
    Table globals;
    ObjUpvalue* openUpvalues;
    int grayCount;
    int grayCapacity;
    Obj** grayStack;
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