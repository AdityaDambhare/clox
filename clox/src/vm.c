#include <stdio.h>
#include <stdarg.h>
#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
VM vm;

static void resetStack(){
    vm.stackTop = vm.stack;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t instruction = vm.ip - vm.chunk->code - 1;
  int line = vm.chunk->lines[instruction].line;
  fprintf(stderr, "[line %d] in script\n", line);
  resetStack();
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

static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define DISPATCH() goto *dispatch_table[instruction]
#define BINARY_OP(valueType,op)\
    do{\
    if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){\
        runtimeError("Operands must be numbers.");\
        return INTERPRET_RUNTIME_ERROR;\
    }\
    double b = AS_NUMBER(pop());\
    double a = AS_NUMBER(pop());\
    push(valueType(a op b));\
    }\
    while(0)\

    uint8_t instruction;
    static void* dispatch_table[] = {&&RETURN,&&CONSTANT,&&NIL,&&TRUE,&&FALSE,&&EQUAL,&&GREATER,&&LESS,&&CONSTANT_LONG,&&ADD,&&SUBTRACT,&&MULTIPLY,&&DIVIDE,&&NOT,&&NEGATE,&&POP};
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

    if(instruction<0 || instruction>=sizeof(dispatch_table)/sizeof(dispatch_table[0])){
        return INTERPRET_RUNTIME_ERROR;
    }
    DISPATCH();
    RETURN:
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
    CONSTANT:
        Value constant = READ_CONSTANT();
        push(constant);
        goto JUMP;
    NIL:
        push(NIL_VAL);
        goto JUMP;
    TRUE:
        push(BOOL_VAL(true));
        goto JUMP;
    FALSE:
        push(BOOL_VAL(false));
        goto JUMP;
    GREATER:
        BINARY_OP(BOOL_VAL,>);goto JUMP;
    EQUAL:
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a,b)));
        goto JUMP;
    LESS:
        BINARY_OP(BOOL_VAL,<);goto JUMP;
    CONSTANT_LONG:
        uint8_t high_byte = READ_BYTE();
        uint8_t low_byte = READ_BYTE();
        uint16_t combined = (high_byte<<8)|low_byte;
        constant = vm.chunk->constants.values[combined];
        push(constant);
        goto JUMP;    
    ADD:
        BINARY_OP(NUMBER_VAL,+);goto JUMP;
    SUBTRACT:
        BINARY_OP(NUMBER_VAL,-);goto JUMP;
    MULTIPLY:
        BINARY_OP(NUMBER_VAL,*);goto JUMP;
    DIVIDE:
        BINARY_OP(NUMBER_VAL,/);goto JUMP;
    NOT:
        push(BOOL_VAL(isFalsey(pop())));goto JUMP;
    NEGATE:
        if(!IS_NUMBER(peek(0))){
            runtimeError("Operand must be a number.");
            return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(AS_NUMBER(pop())));goto JUMP;
    POP:
        pop();goto JUMP;
#undef BINARY_OP        
#undef READ_CONSTANT
#undef DISPATCH
#undef READ_BYTE
}

InterpretResult interpret(const char* source){
    Chunk chunk;
    initChunk(&chunk);
    if(!compile(source,&chunk)){
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    vm.chunk = &chunk;
    vm.ip = vm.chunk->code;
    InterpretResult result = run();
    return result;
}