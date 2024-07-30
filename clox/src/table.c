#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "table.h"

static void adjustCapacity(Table* table,int capacity);

void initTable(Table* table,int capacity){
    table->count =0;
    table->capacity = 0;
    table->entries = NULL;
    adjustCapacity(table,capacity);
}

void freeTable(Table* table){
    FREE_ARRAY(Entry,table->entries,table->capacity);
    initTable(table,0);
}



static Entry* findEntry(Entry* entries, int capacity,ObjString* key) {
  uint32_t index = key->hash & (capacity-1);
  Entry* tombstone = NULL;
  for (;;) {
    Entry* entry = &entries[index];
    if(entry->key==NULL){
        if(IS_NIL(entry->value)){
            //found empty entry
            return tombstone!= NULL? tombstone:entry; // return last tombstone if empty entry found
        }
        else{
            if(tombstone==NULL) tombstone = entry;//mark the found tombstone and keep probing 
        }
    }
    else if(entry->key==key){
        return entry;
    }
    index = (index + 1) & (capacity-1);
  }
}

void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (entry->key != NULL) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString* tableFindString(Table* table,const char* chars,int length,uint32_t hash){
    if(table->count==0) return NULL;
    uint32_t index = hash &(table->capacity-1);
    for(;;){
        Entry* entry = &table->entries[index];
        if(entry->key==NULL){
            if(IS_NIL(entry->value)) return NULL;//non tombstone empty entry
        }
        else if(entry->key->length==length&&memcmp(entry->key->chars,chars,length)==0){
            return entry->key;
        }
        index = (index+1)&(table->capacity-1);
    }
    return NULL;
}

void markTable(Table* table){
    for(int i = 0;i<table->capacity;i++){
        Entry* entry = &table->entries[i];
        markObject((Obj*)entry->key);   
        markValue(entry->value);
    }
}

void tableRemoveWhite(Table* table){
    for(int i =0;i<table->capacity;i++){
        Entry* entry = &table->entries[i];
        if(entry->key!=NULL&&!entry->key->obj.isMarked){
            tableDelete(table,entry->key);//after sweep() deletes the object , it won't create a dangling pointer
        }
    }
}

static void adjustCapacity(Table* table,int capacity){
    Entry* entries = ALLOCATE(Entry,capacity);
    for(int i =0;i<capacity;i++){
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }
    //expensive operation , try inititializing with large capaicity to prevent repetition
    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key == NULL) continue;
    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
    }

    FREE_ARRAY(Entry,table->entries,table->capacity);
    table->entries =entries;
    table->capacity = capacity;
}

bool tableGet(Table* table,ObjString* key,Value* value){
    if(table->count==0) return false;
    Entry* entry = findEntry(table->entries,table->capacity,key);
    if(entry->key==NULL) return false;
    *value = entry->value;
    return true;
}

bool tableSet(Table* table,ObjString* key,Value value){
    if(table->count+1>table->capacity*TABLE_MAX_LOAD){
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table,capacity);
    }
    Entry* entry = findEntry(table->entries,table->capacity,key);
    bool isNewKey = entry->key==NULL;
    if(isNewKey&&IS_NIL(entry->value)){table->count++;}
    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table,ObjString* key){
    if(table->count==0) return false;
    Entry* entry = findEntry(table->entries,table->capacity,key);
    if(entry->key==NULL) return false;//key not found or already deleted
    entry->key = NULL;
    entry->value = BOOL_VAL(false);//create tombstone
    return true;
}