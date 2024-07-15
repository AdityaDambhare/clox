#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, const char* argv[]) {
  Chunk chunk;
  initChunk(&chunk);
  writeChunk(&chunk,OP_RETURN,1);
  for(int i = 0;i<280;i++){
    writeConstant(&chunk,(double)i,i);
  }
  dissassembleChunk(&chunk,"test chunk");
  freeChunk(&chunk);
  
  return 0;
}