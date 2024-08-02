#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include "table.h"
#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)


static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;
  object->next = vm.objects;
  object->isMarked = false;
  vm.objects = object;
#ifdef DEBUG_LOG_GC
  printf("%p allocate %ld for %d %s\n", (void*)object, size, type,objTypeName(type));
#endif
  return object;
}

ObjBoundMethod* newBoundMethod(Value reciever,ObjClosure* method){
  ObjBoundMethod* bound = ALLOCATE_OBJ(ObjBoundMethod,OBJ_BOUND_METHOD);
  bound->receiver = reciever;
  bound->method = method;
  return bound;
}

ObjClosure* newClosure(ObjFunction* function) {
  ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*,function->upvalueCount);
  for (int i = 0; i < function->upvalueCount; i++) {
    upvalues[i] = NULL;
  }
  ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalueCount = function->upvalueCount;
  return closure;
}

ObjInstance* newInstance(ObjClass* klass){
  ObjInstance* instance = ALLOCATE_OBJ(ObjInstance,OBJ_INSTANCE);
  instance->klass = klass;
  initTable(&instance->fields,0);
  return instance;
}

ObjClass* newClass(ObjString* name) {
  ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);//use klass as identifier so that c++ compiler does not get confused with class keyword
  klass->name = name;
  initTable(&klass->methods,0);
  return klass;
}

ObjFunction* newFunction(){
  ObjFunction* function = ALLOCATE_OBJ(ObjFunction,OBJ_FUNCTION);
  function->arity=0;
  function->upvalueCount = 0;
  function->name=NULL;
  initChunk(&function->chunk);
  return function;
}

ObjNative* newNative(NativeFn function) {
  ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  native->function = function;
  return native;
}

ObjList* newList(){
  ObjList* list = ALLOCATE_OBJ(ObjList,OBJ_LIST);
  initValueArray(&list->objects);
  return list;
}

static ObjString* allocateString(char* chars, int length,uint32_t hash) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  push(OBJ_VAL(string));
  tableSet(&vm.strings,string, NIL_VAL);
  pop();
  return string;
}

static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

ObjString* takeString(char* chars, int length) {
  uint32_t hash = hashString(chars,length);
  ObjString* interned = tableFindString(&vm.strings, chars, length,hash);
  if(interned!=NULL) {
    
    FREE_ARRAY(char,chars,length+1);
    return interned;
  }
  return allocateString(chars, length,hash);
}

ObjString* copyString(const char* chars, int length) {
  uint32_t hash = hashString(chars,length);
  ObjString* interned = tableFindString(&vm.strings, chars, length,hash);
  if(interned!=NULL) return interned;
  
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(heapChars, length,hash);
}   

ObjUpvalue* newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  upvalue->location = slot;
  upvalue->closed = NIL_VAL;
  upvalue->next = NULL;
  return upvalue;
}

void printFunction(ObjFunction* function) {
  if (function->name == NULL) {
    printf("<script>");
    return;
  }
  printf("<fn %s>", function->name->chars);
}
void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_BOUND_METHOD:
      printFunction(AS_BOUND_METHOD(value)->method->function);
      break;
    case OBJ_CLASS:
      printf("%s", AS_CLASS(value)->name->chars);
      break;
    case OBJ_INSTANCE:
      printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
      break;
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
    case OBJ_FUNCTION:
      printFunction(AS_FUNCTION(value));
      break;
    case OBJ_NATIVE:
      printf("<native fn>");
      break;
    case OBJ_CLOSURE:
      printFunction(AS_CLOSURE(value)->function);
      break;
     case OBJ_UPVALUE:
      printf("upvalue");
      break;
    case OBJ_LIST:
      printf("<list : %u>",((ObjList*)AS_OBJ(value))->objects.count);
      break;
  }
}

const char* objTypeName(ObjType type){
  switch(type){
    case OBJ_BOUND_METHOD:
      return "BOUND_METHOD";
    case OBJ_CLASS:
      return "CLASS";
    case OBJ_INSTANCE:
      return "INSTANCE";
    case OBJ_STRING:
      return "STRING";
    case OBJ_FUNCTION:
      return "FUNCTION";
    case OBJ_NATIVE:
      return "NATIVE";
    case OBJ_CLOSURE:
      return "CLOSURE";
    case OBJ_UPVALUE:
      return "UPVALUE";
    case OBJ_LIST:
      return "LIST";
  }
  return "UNKNOWN";
}