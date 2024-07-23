#ifndef CLOX_VM_H
#define CLOX_VM_H
#define STACK_MAX UINT16_MAX
#include "chunk.h"
#include "value.h"
#include "table.h"
typedef struct{
    Chunk* chunk;
    uint8_t* ip;
    Value stack[STACK_MAX];
    Value* stackTop;
    Obj* objects;
    Table strings;
    Table globals;
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