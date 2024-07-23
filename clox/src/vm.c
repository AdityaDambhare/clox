#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"
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
    vm.objects = NULL;
    initTable(&vm.strings);
    initTable(&vm.globals);
}

void freeVM(){
    freeObjects();
    freeTable(&vm.strings);
    freeTable(&vm.globals);
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

ObjString* value_to_string(Value value, int precision) {
    int needed_size;
    char *result;

    // Use snprintf to determine the required buffer size
    needed_size = snprintf(NULL, 0, "%.*g", precision, (double)AS_NUMBER(value));

    // Check for errors or insufficient space allocation
    if (needed_size <= 0) {
        return NULL; // Handle error or insufficient space
    }

    // Allocate memory for the string (including null terminator)
    result = ALLOCATE(char,needed_size + 1);
    if (result == NULL) {
        return NULL; // Handle memory allocation failure
    }

    // Convert the double to the string with the specified precision
    snprintf(result, needed_size + 1, "%.*g", precision, (double)AS_NUMBER(value));
    
    return takeString(result, needed_size);
}

static void concatenate() {
  Value b_val =  pop();
  Value a_val =  pop();
  ObjString* a = IS_NUMBER(a_val)?value_to_string(a_val,10):AS_STRING(a_val);
  ObjString* b = IS_NUMBER(b_val)?value_to_string(b_val,10):AS_STRING(b_val);
  int length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  push(OBJ_VAL(result));
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
static void* dispatch_table[] = 
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
  &&SET_LOCAL
  };
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
        {
        if ((IS_STRING(peek(0)) && IS_NUMBER(peek(1)))
        || (IS_NUMBER(peek(0)) && IS_STRING(peek(1)))
        || (IS_STRING(peek(0)) && IS_STRING(peek(1)))
        ) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } 
        else {
          runtimeError(
              "Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        goto JUMP;
      }
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
    POWER:
        {
            if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){
                runtimeError("Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }
            double b = AS_NUMBER(pop());
            double a = AS_NUMBER(pop());
            push(NUMBER_VAL(pow(a,b)));
        }
        goto JUMP;
    POP:
        pop();goto JUMP;
    PRINT:
        printValue(pop());
        printf("\n");
        goto JUMP;
    DEFINE_GLOBAL:
        {
            ObjString* name = AS_STRING(READ_CONSTANT());
            tableSet(&vm.globals,name,peek(0));
            pop();
            goto JUMP;
        }
    DEFINE_GLOBAL_LONG:
        {
            uint8_t high_byte = READ_BYTE();
            uint8_t low_byte = READ_BYTE();
            uint16_t combined = (high_byte<<8)|low_byte;
            ObjString* name = AS_STRING(vm.chunk->constants.values[combined]);
            tableSet(&vm.globals,name,peek(0));
            pop();
            goto JUMP;
        }
    GET_GLOBAL:
        {
            ObjString* name = AS_STRING(READ_CONSTANT());
            Value value;
            if(!tableGet(&vm.globals,name,&value)){
                runtimeError("Undefined variable '%s'.",name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(value);
            goto JUMP;
        }
    GET_GLOBAL_LONG:
        {
            uint8_t high_byte = READ_BYTE();
            uint8_t low_byte = READ_BYTE();
            uint16_t combined = (high_byte<<8)|low_byte;
            ObjString* name = AS_STRING(vm.chunk->constants.values[combined]);
            Value value;
            if(!tableGet(&vm.globals,name,&value)){
                runtimeError("Undefined variable '%s'.",name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(value);
            goto JUMP;
        }
    SET_GLOBAL:
        {
            ObjString* name = AS_STRING(READ_CONSTANT());
            if(tableSet(&vm.globals,name,peek(0))){//tableSet returns true is the key is new
                tableDelete(&vm.globals,name);
                runtimeError("Undefined variable '%s'.",name->chars);//in that case the variable must be undefined
                return INTERPRET_RUNTIME_ERROR;
            }
            goto JUMP;
        }
    SET_GLOBAL_LONG:
        {
            uint8_t high_byte = READ_BYTE();
            uint8_t low_byte = READ_BYTE();
            uint16_t combined = (high_byte<<8)|low_byte;
            ObjString* name = AS_STRING(vm.chunk->constants.values[combined]);
            if(tableSet(&vm.globals,name,peek(0))){
                tableDelete(&vm.globals,name);
                runtimeError("Undefined variable '%s'.",name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            goto JUMP;
        
        }
    GET_LOCAL:
        {
            uint8_t high_byte = READ_BYTE();
            uint8_t low_byte = READ_BYTE();
            uint16_t combined = (high_byte<<8)|low_byte;
            push(vm.stack[combined]);
            goto JUMP;
        }
    SET_LOCAL:
        {
            uint8_t high_byte = READ_BYTE();
            uint8_t low_byte = READ_BYTE();
            uint16_t combined = (high_byte<<8)|low_byte;
            vm.stack[combined] = peek(0);
            goto JUMP;
        }
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