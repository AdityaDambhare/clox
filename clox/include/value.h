#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H
typedef double Value;
typedef struct {
    int count;
    int capacity;
    Value* values;
} ValueArray;
void writeValueArray(ValueArray* array, Value value);
void initValueArray(ValueArray* array);
void freeValueArray(ValueArray* array);
void printValue(Value value);
#endif