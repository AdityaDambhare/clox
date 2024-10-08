#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H
#include "common.h"
#include "value.h"
#include "chunk.h"
#include "table.h"

#define OBJ_TYPE(value) (AS_OBJ(value)->type)
#define IS_STRING(value)       isObjType(value, OBJ_STRING) 
#define IS_NATIVE(value)       isObjType(value, OBJ_NATIVE)
#define IS_LIST(value)         isObjType(value,OBJ_LIST)
#define AS_STRING(value)       ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)      (((ObjString*)AS_OBJ(value))->chars)
#define IS_CLOSURE(value)      isObjType(value, OBJ_CLOSURE)
#define AS_LIST(value)         ((ObjList*)AS_OBJ(value))
#define IS_CLASS(value)        isObjType(value, OBJ_CLASS)  
#define AS_CLASS(value)        ((ObjClass*)AS_OBJ(value))
#define IS_BOUND_METHOD(value) isObjType(value,OBJ_BOUND_METHOD)
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))
#define IS_INSTANCE(value)     isObjType(value, OBJ_INSTANCE)
#define AS_INSTANCE(value)     ((ObjInstance*)AS_OBJ(value))
#define AS_FUNCTION(value)     ((ObjFunction*)AS_OBJ(value))
#define AS_CLOSURE(value)      ((ObjClosure*)AS_OBJ(value))
#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function)

typedef enum{
    OBJ_STRING,
    OBJ_FUNCTION,
    OBJ_NATIVE,
    OBJ_CLOSURE,
    OBJ_UPVALUE,
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_LIST
}ObjType;

struct Obj{
    ObjType type;
    Obj* next;
    bool isMarked;
}; 

typedef struct{
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk chunk;
    ObjString* name;
}ObjFunction;



struct ObjString{
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;//precalculated for every string 
}; 

typedef struct ObjUpvalue{
  Obj obj;
  Value* location;
  Value closed;
  struct ObjUpvalue* next;
}ObjUpvalue;

typedef struct {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
} ObjClosure;

typedef struct {
  Obj obj;
  ObjString* name;
  Table methods;
}ObjClass;

typedef struct {
  Obj obj;
  ObjClass* klass;
  Table fields; 
} ObjInstance;

typedef struct {
  Obj obj;
  Value receiver;
  ObjClosure* method;
} ObjBoundMethod;

typedef struct{
    Obj obj;
    ValueArray objects;
}ObjList;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;

const char* objTypeName(ObjType type);

ObjClass* newClass(ObjString* name);
ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* method);
ObjInstance* newInstance(ObjClass* klass);
ObjClosure* newClosure(ObjFunction* function);
ObjFunction* newFunction();
ObjNative* newNative(NativeFn function);
ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);  
ObjUpvalue* newUpvalue(Value* slot); 
ObjList* newList();
void printObject(Value value);
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

#endif