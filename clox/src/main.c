#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

int main(int argc, const char* argv[]) {
  Chunk chunk;
  initChunk(&chunk);
  for(int i = 0;i<20;i++){
    writeConstant(&chunk,(double)i,i);
    writeChunk(&chunk,OP_NEGATE,i);
  }
  writeChunk(&chunk,OP_RETURN,200);
  initVM();
  interpret(&chunk);
  freeChunk(&chunk);
  
  return 0;
}