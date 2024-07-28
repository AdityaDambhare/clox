#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "common.h"
#include "debug.h"
#include "vm.h"
#include "compiler.h"
#include "object.h"
#include "memory.h"
VM vm;

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack(){
    vm.frameCount = 0;
    vm.stackTop = vm.stack;
    vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);
   for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", 
            getLine(&function->chunk, instruction));
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }
  resetStack();
}

static void defineNative(const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

void initVM(){
    resetStack();
    vm.objects = NULL;
    initTable(&vm.strings,64);
    initTable(&vm.globals,64);
    vm.bytesAllocated = 0;
    vm.nextGC = 1024 * 256;
    vm.grayCount = 0;
    vm.grayCapacity = 0;
    vm.grayStack = NULL;
    defineNative("clock", clockNative);
}

void freeVM(){
    freeObjects();
    freeTable(&vm.strings);
    freeTable(&vm.globals);
    free(vm.grayStack);
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

static bool call(ObjClosure* closure, int argCount) {
  if(argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.",
        closure->function->arity, argCount);
    return false;
  }
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }
  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value result = native(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount + 1;
        push(result);
        return true;
      }
      case OBJ_CLOSURE:
        return call(AS_CLOSURE(callee), argCount);
      default:
        break; // Non-callable object type.
  }
  runtimeError("Can only call functions and classes.");
  return false;
}
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while(upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }
  if(upvalue!=NULL && upvalue->location == local) return upvalue;
  ObjUpvalue* createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;
  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }
  return createdUpvalue;
}

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL &&
         vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
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
  Value b_val =  peek(0);
  Value a_val =  peek(1);
  ObjString* a = IS_NUMBER(a_val)?value_to_string(a_val,10):AS_STRING(a_val);
  ObjString* b = IS_NUMBER(b_val)?value_to_string(b_val,10):AS_STRING(b_val);
  int length = a->length + b->length;

  char* chars = ALLOCATE(char, length + 1);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';

  ObjString* result = takeString(chars, length);
  pop();pop();
  push(OBJ_VAL(result));
}

static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  register uint8_t* ip = frame->ip;
#define READ_BYTE() (*ip++)
#define READ_SHORT() \
    ((uint16_t)((READ_BYTE() << 8) | READ_BYTE()))

#define READ_CONSTANT() \
    (frame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_CONSTANT_LONG() \
    (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define DISPATCH() goto *dispatch_table[instruction]
#define BINARY_OP(valueType,op)\
    do{\
    if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){\
        frame->ip = ip;\
        runtimeError("Operands must be numbers.");\
        return INTERPRET_RUNTIME_ERROR;\
    }\
    double b = AS_NUMBER(pop());\
    double a = AS_NUMBER(pop());\
    push(valueType(a op b));\
    }\
    while(0)\

    register uint8_t instruction;
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
  &&SET_LOCAL,
  &&JUMPOP,
  &&JUMP_IF_FALSE,
  &&LOOP,
  &&CALL,
  &&CLOSURE,
  &&GET_UPVALUE,
  &&SET_UPVALUE,
  &&CLOSE_UPVALUE
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
    frame->ip = ip;
    printf("\n");
    dissassembleInstruction(&frame->closure->function->chunk,
        (int)(ip - frame->closure->function->chunk.code-1));
#endif

    DISPATCH();
    RETURN:
        {
        Value result = pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
            pop();
          return INTERPRET_OK;
        }
        vm.stackTop = frame->slots;
        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        goto JUMP;
        }
    CONSTANT:{
        Value constant = READ_CONSTANT();
        push(constant);
    }
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
    EQUAL:{
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a,b)));
        }
        goto JUMP;
    LESS:
        BINARY_OP(BOOL_VAL,<);goto JUMP;
    CONSTANT_LONG:{
        Value constant = READ_CONSTANT_LONG();
        push(constant);
    }
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
            frame->ip = ip; 
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
    NEGATE:{
        if(!IS_NUMBER(peek(0))){
            frame->ip = ip;
            runtimeError("Operand must be a number.");
            return INTERPRET_RUNTIME_ERROR;
        }
        Value value = pop();
        push(NUMBER_VAL(-AS_NUMBER(value)));
        goto JUMP;
    }
    POWER:
        {
            if(!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))){
                frame->ip = ip;
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
            ObjString* name = AS_STRING(READ_CONSTANT_LONG());
            tableSet(&vm.globals,name,peek(0));
            pop();
            goto JUMP;
        }
    GET_GLOBAL:
        {
            ObjString* name = AS_STRING(READ_CONSTANT());
            Value value;
            if(!tableGet(&vm.globals,name,&value)){
                frame->ip = ip;
                runtimeError("Undefined variable '%s'.",name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            push(value);
            goto JUMP;
        }
    GET_GLOBAL_LONG:
        {
            ObjString* name = AS_STRING(READ_CONSTANT_LONG());
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
                frame->ip = ip;
                tableDelete(&vm.globals,name);
                runtimeError("Undefined variable '%s'.",name->chars);//in that case the variable must be undefined
                return INTERPRET_RUNTIME_ERROR;
            }
            goto JUMP;
        }
    SET_GLOBAL_LONG:
        {
            ObjString* name = AS_STRING(READ_CONSTANT_LONG());
            if(tableSet(&vm.globals,name,peek(0))){
                frame->ip = ip;
                tableDelete(&vm.globals,name);
                runtimeError("Undefined variable '%s'.",name->chars);
                return INTERPRET_RUNTIME_ERROR;
            }
            goto JUMP;
        
        }
    GET_LOCAL:
        {
            uint16_t combined = READ_SHORT();
            push(frame->slots[combined]);
            goto JUMP;
        }
    SET_LOCAL:
        {
            uint16_t combined = READ_SHORT();
            frame->slots[combined] = peek(0);
            goto JUMP;
        }
    JUMPOP:
        {
            uint16_t combined = READ_SHORT();
            ip += combined;
            goto JUMP;
        }
    JUMP_IF_FALSE:
        {   

            uint16_t combined = READ_SHORT();
            if(isFalsey(peek(0))){
                ip += combined;
            }
            goto JUMP;
        }
    LOOP:
        {
            uint16_t combined = READ_SHORT();
            ip -= combined;
            goto JUMP;
        }
    CALL:
        {
        uint8_t argCount = READ_BYTE();
        frame->ip = ip;
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        }
        goto JUMP;
    CLOSURE:
        {   
            ObjFunction* function = AS_FUNCTION(READ_CONSTANT_LONG());
            ObjClosure* closure = newClosure(function);
            push(OBJ_VAL(closure));
            for (int i = 0; i < closure->upvalueCount; i++) {
                uint8_t isLocal = READ_BYTE();
                uint16_t index = READ_SHORT();
                if (isLocal) {
                    closure->upvalues[i] =
                        captureUpvalue(frame->slots + index);
                } else {
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }
            goto JUMP;
        }
    GET_UPVALUE:
    {
        uint16_t slot = READ_SHORT();
        push(*frame->closure->upvalues[slot]->location);
        goto JUMP;
    }
    SET_UPVALUE:
    {
        uint16_t slot = READ_SHORT();
        *frame->closure->upvalues[slot]->location = peek(0);
        goto JUMP;
    }
    CLOSE_UPVALUE:
    {
        closeUpvalues(vm.stackTop - 1);
        pop();
        goto JUMP;
    }
#undef BINARY_OP        
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_CONSTANT_LONG
#undef DISPATCH
#undef READ_BYTE
}

InterpretResult interpret(const char* source){
    ObjFunction* function = compile(source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    push(OBJ_VAL(function));
    ObjClosure* closure = newClosure(function);
    pop();
    push(OBJ_VAL(closure));
    call(closure, 0);
    return run();
}