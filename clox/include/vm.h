#ifndef CLOX_VM_H
#define CLOX_VM_H
#define STACK_MAX 1024
#include "chunk.h"
#include "value.h"

typedef struct{
    Chunk* chunk;
    uint8_t* ip;
    Value stack[STACK_MAX];
    Value* stackTop;
    Obj* objects;
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