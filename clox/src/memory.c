#include <stdlib.h>
#include "memory.h"
#include "object.h"
#include "vm.h" 
#include "compiler.h"
#define GC_HEAP_GROW_FACTOR 2
#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif
void *reallocate(void *pointer, size_t oldSize, size_t newSize) {
  vm.bytesAllocated += newSize - oldSize;
  if(newSize>oldSize){
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
  if(vm.bytesAllocated > vm.nextGC){
    collectGarbage();
  }

  }

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }
    void *result = realloc(pointer, newSize);
    if (result == NULL) exit(1);
    return result;
}
#define FREE(type,pointer) reallocate(pointer,sizeof(type),0)
static void freeObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p free type %d %s\n", (void*)object, object->type,objTypeName(object->type));
#endif
  switch (object->type) {
    case OBJ_BOUND_METHOD:
      FREE(ObjBoundMethod, object);
      break;
    case OBJ_CLASS:
      ObjClass* klass = (ObjClass*)object;
      freeTable(&klass->methods);
      FREE(ObjClass, object);
      break;
    case OBJ_INSTANCE:
      ObjInstance* instance = (ObjInstance*)object;
      freeTable(&instance->fields);
      FREE(ObjInstance, object);
      break;
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_FUNCTION:{
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction,object);
      break;
    }
    case OBJ_NATIVE:
      FREE(ObjNative, object);
      break;
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues,closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object);
      break;
    case OBJ_LIST:{
      ObjList* list = (ObjList*)object;
      freeValueArray(&list->objects);
      FREE(ObjList,object);
      break;
    }
  }
}

void markObject(Obj *object){
  if(object == NULL ) return;
  if(object->isMarked) return;
#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  object->isMarked = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (Obj**)realloc(vm.grayStack,sizeof(Obj*) * vm.grayCapacity);
    if(vm.grayStack == NULL) exit(1);
  }

  vm.grayStack[vm.grayCount++] = object;
}

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

void markValue(Value slot){
    if(IS_OBJ(slot)){
        markObject(AS_OBJ(slot));
    }
}

static void blackenObject(Obj* object){
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif
  switch(object->type){
    case OBJ_BOUND_METHOD:{
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      markValue(bound->receiver);
      markObject((Obj*)bound->method);
      break;
    }
    case OBJ_CLASS:{
      ObjClass* klass = (ObjClass*)object;
      markObject((Obj*)klass->name);//keep name alive
      markTable(&klass->methods);
      break;
    }
    case OBJ_INSTANCE:{
      ObjInstance* instance = (ObjInstance*)object;
      markObject((Obj*)instance->klass);
      markTable(&instance->fields);
      break;
    }
    case OBJ_CLOSURE:{
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for(int i = 0; i<closure->upvalueCount;i++){
        markObject((Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FUNCTION:
    ObjFunction* function = (ObjFunction*)object;
    markObject((Obj*)function->name);
    markArray(&function->chunk.constants);
    break;
    case OBJ_LIST:{
      ObjList* list = (ObjList*)object;
      markArray(&list->objects);
      break;
    }
    case OBJ_UPVALUE:
    markValue(((ObjUpvalue*)object)->closed);
    break;
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}

static void markRoots(){
  for(Value* slot = vm.stack;slot<vm.stackTop;slot++){//mark stack
    markValue(*slot);
  }
  for(int i = 0; i < vm.frameCount; i++){//mark frames
    markObject((Obj*)vm.frames[i].closure);
  }
  for (ObjUpvalue* upvalue = vm.openUpvalues;upvalue != NULL;upvalue = upvalue->next){//mark upvalues
    markObject((Obj*)upvalue);
  }
  markTable(&vm.globals);//mark globals table
  markCompilerRoots();//mark compiler roots . the compiler accesses runtime memory so we need to mark it
}
static void traceReferences(){
  while(vm.grayCount > 0){
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

static void sweep(){
  Obj* previous = NULL;
  Obj* object = vm.objects;
  while(object != NULL){
    if(object->isMarked){
      object->isMarked = false;
      previous = object;
      object = object->next;
    }
    else{
      Obj* unreached = object;
      object = object->next;
      if(previous != NULL){
        previous->next = object;
      }
      else{
        vm.objects = object;
      }
      freeObject(unreached);
    }
  }
}

void freeObjects(){
  Obj* object = vm.objects;
  while(object != NULL){
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}



void collectGarbage(){
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif
  markObject((Obj*)vm.initString);
  markRoots();
  traceReferences();
  tableRemoveWhite(&vm.strings);
  sweep();
  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC);
#endif
}


