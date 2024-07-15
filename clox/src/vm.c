#include <stdio.h>
#include "common.h"
#include "debug.h"
#include "vm.h"
VM vm;

static void resetStack(){
    vm.stackTop = vm.stack;
}

void initVM(){
    resetStack();
}

void freeVM(){

}   



void push(Value value){
    *vm.stackTop =value;
    vm.stackTop++;
}

Value pop(){
    vm.stackTop--;
    return *vm.stackTop;
}


static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define DISPATCH() goto *dispatch_table[instruction]
#define BINARY_OP(op)\
    do{\
    Value b = pop();\
    Value a = pop();\
    push(a op b);\
    }\
    while(0)\

    uint8_t instruction;
    static void* dispatch_table[8] = {&&RETURN,&&CONSTANT,&&CONSTANT_LONG,&&ADD,&&SUBTRACT,&&MULTIPLY,&&DIVIDE,&&NEGATE};
    JUMP:
    instruction = READ_BYTE();



#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    dissassembleInstruction(vm.chunk,(int)(vm.ip-vm.chunk->code-1));
#endif


    DISPATCH();
    RETURN:
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
    CONSTANT:
        Value constant = READ_CONSTANT();
        push(constant);
        goto JUMP;
    CONSTANT_LONG:
        uint8_t high_byte = READ_BYTE();
        uint8_t low_byte = READ_BYTE();
        uint16_t combined = (high_byte<<8)|low_byte;
        constant = vm.chunk->constants.values[combined];
        push(constant);
        goto JUMP;    
    ADD:
        BINARY_OP(+);goto JUMP;
    SUBTRACT:
        BINARY_OP(-);goto JUMP;
    MULTIPLY:
        BINARY_OP(*);goto JUMP;
    DIVIDE:
        BINARY_OP(/);goto JUMP;
    NEGATE:
        push(-pop());goto JUMP;

#undef BINARY_OP        
#undef READ_CONSTANT
#undef DISPATCH
#undef READ_BYTE
}

InterpretResult interpret(Chunk* chunk){
    vm.chunk = chunk;
    vm.ip = chunk->code;
    return run();
}