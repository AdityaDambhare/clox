#ifndef clox_trace_h
#define clox_trace_h

/*
  Emits newline JSON (NDJSON) to stdout so the
  browser visualizer can replay each VM step.
  
  Message types emitted in order:
    {"type":"init",  "bytecode":[...], "constants":[...], "lines":[...]}
    {"type":"step",  "ip":N, "op":"OP_FOO", "operand":M,  -- emitted BEFORE each instruction
                    "stack":[...], "globals":{...}}
    {"type":"out",   "val":"..."}                          -- on OP_PRINT
    {"type":"error", "msg":"..."}                          -- on runtime error
    {"type":"done"}                                        -- on clean RETURN
 */

#include <stdio.h>
#include <stdbool.h>
#include "chunk.h"
#include "value.h"


void traceInit(FILE* out, Chunk* chunk);


void traceStep(FILE* out, int ip, uint8_t nextOp);

void traceOutput(FILE* out, Value val);


void traceError(FILE* out, const char* msg);


void traceDone(FILE* out);

#endif
