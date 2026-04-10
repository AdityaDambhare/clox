#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "chunk.h"
#include "debug.h"
#include "vm.h"

static void repl(){
  char line[1024];
  for(;;){
    printf("> ");
    if(!fgets(line,sizeof(line),stdin)){
      printf("\n");
      break;
    }
    interpret(line);
  }

}

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if(file==NULL){
    fprintf(stderr,"Could not open file \"%s\".\n",path);
    exit(74);
  }
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file); //figure out file size
  rewind(file);

  char* buffer = (char*)malloc(fileSize + 1);
  if(buffer==NULL){
    fprintf(stderr,"Not enough memory to read \"%s\".\n",path);
    exit(74);
  }
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  buffer[bytesRead] = '\0';
  if(bytesRead<fileSize){
    fprintf(stderr,"Could not read file \"%s\".\n",path);
    exit(74);
  }
  fclose(file);
  return buffer;
}

static void runFile(const char* path) {
  char* source = readFile(path);
#ifdef DUMP_PROGRAM_OUTPUT
  freopen("output.txt","w",stdout);
#endif
  InterpretResult result = interpret(source);
  free(source); 

  if (result == INTERPRET_COMPILE_ERROR) exit(65);
  if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}


int main(int argc, const char* argv[]) {
  bool traceJson = false;
  const char* filePath = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--trace-json") == 0) {
      traceJson = true;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      exit(64);
    } else {
      if (filePath != NULL) {
        fprintf(stderr, "Usage: clox [--trace-json] [path]\n");
        exit(64);
      }
      filePath = argv[i];
    }
  }

  initVM();
  vm.traceJson = traceJson;
  if (traceJson) {
    vm.traceOut = stdout;
  }

  if (filePath == NULL) {
    repl();
  } else {
    runFile(filePath);
  }
  freeVM();
  return 0;
}