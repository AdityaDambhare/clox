#include <stdlib.h> // for NULL

#include "chunk.h"
#include "memory.h"

void initChunk(Chunk* chunk) {
  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  chunk->lineCount = 0;
  chunk->lineCapacity = 0;
  initValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk,uint8_t byte,int line){
    if(chunk->capacity<chunk->count+1){
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(uint8_t,chunk->code,oldCapacity,chunk->capacity);    
    }//increase array size if needed
    chunk->code[chunk->count] = byte;
    chunk->count++;  
    if(chunk->lineCount>0 && chunk->lines[chunk->lineCount-1].line == line){
        return;
    }
    if(chunk->lineCapacity<chunk->lineCount+1){
        int oldCapacity = chunk->lineCapacity;
        chunk->lineCapacity = GROW_CAPACITY(oldCapacity);
        chunk->lines = GROW_ARRAY(LineStart,chunk->lines,oldCapacity,chunk->lineCapacity);
    }
    LineStart* lineStart = &chunk->lines[chunk->lineCount++];
    lineStart->offset = chunk->count-1;
    lineStart->line = line;
}
int addConstant(Chunk* chunk,Value value){
    writeValueArray(&chunk->constants,value);
    return chunk->constants.count-1;
}


bool writeConstant(Chunk* chunk,Value value,int line){
    int index = addConstant(chunk,value);
    if(index<256){
        writeChunk(chunk,OP_CONSTANT,line);
        writeChunk(chunk,index,line);
    }
    else if(index<65536){
        writeChunk(chunk,OP_CONSTANT_LONG,line);
        writeChunk(chunk,(uint8_t)(index>>8),line);
        writeChunk(chunk,(uint8_t)(index&0xff),line);
    }
    else{
        return false;
    }
    return true;
}

void freeChunk(Chunk* chunk){
    FREE_ARRAY(uint8_t,chunk->code,chunk->capacity);
    freeValueArray(&chunk->constants);
    FREE_ARRAY(LineStart,chunk->lines,chunk->capacity);
    initChunk(chunk);
}

int getLine(Chunk* chunk,int offset){
    int start = 0;
    int end = chunk->lineCount-1;
    for(;;){
        int mid = (start+end)/2;
        LineStart* line = &chunk->lines[mid];
        if(offset<line->offset){
            end = mid -1;
        }
        else if (mid == chunk->lineCount-1 || offset < chunk->lines[mid+1].offset){
            return line->line;
        }
        else{
            start = mid+1;
        }
    }
}
