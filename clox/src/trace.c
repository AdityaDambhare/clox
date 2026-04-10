

#include <stdio.h>
#include <string.h>
#include "trace.h"
#include "object.h"
#include "vm.h"      
#include "table.h"


static void jsonStr(FILE* out, const char* s, int len) {
    fputc('"', out);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n",  out); break;
            case '\r': fputs("\\r",  out); break;
            case '\t': fputs("\\t",  out); break;
            default:
                if (c < 0x20) fprintf(out, "\\u%04x", c);
                else          fputc(c, out);
                break;
        }
    }
    fputc('"', out);
}

static void jsonValue(FILE* out, Value val) {
    if (IS_NIL(val)) {
        fputs("{\"k\":\"nil\",\"v\":null}", out);
    } else if (IS_BOOL(val)) {
        fprintf(out, "{\"k\":\"bool\",\"v\":%s}", AS_BOOL(val) ? "true" : "false");
    } else if (IS_NUMBER(val)) {
        fprintf(out, "{\"k\":\"num\",\"v\":%.14g}", AS_NUMBER(val));
    } else if (IS_OBJ(val)) {
        switch (OBJ_TYPE(val)) {
            case OBJ_STRING: {
                ObjString* s = AS_STRING(val);
                fputs("{\"k\":\"str\",\"v\":", out);
                jsonStr(out, s->chars, s->length);
                fputc('}', out);
                break;
            }
            case OBJ_FUNCTION: {
                ObjFunction* f = AS_FUNCTION(val);
                const char* name = f->name ? f->name->chars : "<script>";
                fprintf(out, "{\"k\":\"fn\",\"v\":\"<fn %s>\"}",  name);
                break;
            }
            case OBJ_CLOSURE: {
                ObjClosure* cl = AS_CLOSURE(val);
                const char* name = cl->function->name
                                   ? cl->function->name->chars : "<script>";
                fprintf(out, "{\"k\":\"fn\",\"v\":\"<fn %s>\"}",  name);
                break;
            }
            case OBJ_CLASS: {
                ObjClass* cls = AS_CLASS(val);
                fprintf(out, "{\"k\":\"class\",\"v\":\"<class %s>\"}",
                        cls->name->chars);
                break;
            }
            case OBJ_INSTANCE: {
                ObjInstance* inst = AS_INSTANCE(val);
                fprintf(out, "{\"k\":\"instance\",\"v\":\"<%s instance>\"}",
                        inst->klass->name->chars);
                break;
            }
            case OBJ_BOUND_METHOD: {
                fputs("{\"k\":\"method\",\"v\":\"<bound method>\"}", out);
                break;
            }
            case OBJ_NATIVE: {
                fputs("{\"k\":\"native\",\"v\":\"<native fn>\"}", out);
                break;
            }

            default:
                fputs("{\"k\":\"obj\",\"v\":\"<object>\"}", out);
                break;
        }
    } else {
        fputs("{\"k\":\"unknown\",\"v\":null}", out);
    }
}


static const char* opcodeName(uint8_t op) {
    switch ((OpCode)op) {
        case OP_CONSTANT:       return "OP_CONSTANT";
        case OP_NIL:            return "OP_NIL";
        case OP_TRUE:           return "OP_TRUE";
        case OP_FALSE:          return "OP_FALSE";
        case OP_POP:            return "OP_POP";
        case OP_GET_LOCAL:      return "OP_GET_LOCAL";
        case OP_SET_LOCAL:      return "OP_SET_LOCAL";
        case OP_GET_GLOBAL:     return "OP_GET_GLOBAL";
        case OP_DEFINE_GLOBAL:  return "OP_DEFINE_GLOBAL";
        case OP_SET_GLOBAL:     return "OP_SET_GLOBAL";
        case OP_GET_UPVALUE:    return "OP_GET_UPVALUE";
        case OP_SET_UPVALUE:    return "OP_SET_UPVALUE";
        case OP_GET_PROPERTY:   return "OP_GET_PROPERTY";
        case OP_SET_PROPERTY:   return "OP_SET_PROPERTY";
        case OP_GET_SUPER:      return "OP_GET_SUPER";
        case OP_EQUAL:          return "OP_EQUAL";
        case OP_GREATER:        return "OP_GREATER";
        case OP_LESS:           return "OP_LESS";
        case OP_ADD:            return "OP_ADD";
        case OP_SUBTRACT:       return "OP_SUBTRACT";
        case OP_MULTIPLY:       return "OP_MULTIPLY";
        case OP_DIVIDE:         return "OP_DIVIDE";
        case OP_NOT:            return "OP_NOT";
        case OP_NEGATE:         return "OP_NEGATE";
        case OP_PRINT:          return "OP_PRINT";
        case OP_JUMP:           return "OP_JUMP";
        case OP_JUMP_IF_FALSE:  return "OP_JUMP_IF_FALSE";
        case OP_LOOP:           return "OP_LOOP";
        case OP_CALL:           return "OP_CALL";
        case OP_INVOKE:         return "OP_INVOKE";
        case OP_INVOKE_SUPER:   return "OP_SUPER_INVOKE";
        case OP_CLOSURE:        return "OP_CLOSURE";
        case OP_CLOSE_UPVALUE:  return "OP_CLOSE_UPVALUE";
        case OP_RETURN:         return "OP_RETURN";
        case OP_CLASS:          return "OP_CLASS";
        case OP_INHERIT:        return "OP_INHERIT";
        case OP_METHOD:         return "OP_METHOD";

        default: {
            static char buf[16];
            snprintf(buf, sizeof(buf), "%x", op);
            return buf;
        }
    }
}

/* ── public API ───────────────────────────────────────────── */

void traceInit(FILE* out, ObjFunction* function) {
    Chunk* chunk = &(function->chunk);
    fputs("{\"type\": \"init\" ,", out);

    fputs("\"bytecode\":[", out);
    for (int i = 0; i < chunk->count; i++) {
        if (i) fputc(',', out);
        fprintf(out, "%d", chunk->code[i]);
    }
    fputs("],", out);

    /* constants pool */
    fputs("\"constants\":[", out);
    for (int i = 0; i < chunk->constants.count; i++) {
        if (i) fputc(',', out);
        jsonValue(out, chunk->constants.values[i]);
    }
    fputs("],", out);

    /* source lines (parallel to bytecode) */
    fputs("\"lines\":[", out);
    int lstart = 0;
    int linecount = chunk->lineCount;
    int currline = 1;
    for (int i = 0; i < chunk->count; i++) {
        if (i) fputc(',', out);
        int k = 0;
        while(chunk->lines[k].offset<i){
            k++;
        }
        if(chunk->lines[k].offset==i){
            k++;
        }
        fprintf(out, "%d", chunk->lines[k-1].line);
    }
    fputs("]}\n", out);
    fflush(out);
}

void traceStep(FILE* out, int ip, uint8_t nextOp) {

    fprintf(out, "{\"type\":\"step\",\"ip\":%d,\"op\":\"%s\",",
            ip, opcodeName(nextOp));

    /* ── stack ── (bottom → top so index 0 = deepest) */
    fputs("\"stack\":[", out);
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        if (slot != vm.stack) fputc(',', out);
        jsonValue(out, *slot);
    }
    fputs("],", out);

    /* ── globals ── iterate the hash table, skip tombstones */
    fputs("\"globals\":{", out);
    bool firstGlobal = true;
    for (int i = 0; i < vm.globals.capacity; i++) {
        Entry* entry = &vm.globals.entries[i];
        if (entry->key == NULL) continue;
        if (!firstGlobal) fputc(',', out);
        firstGlobal = false;
        jsonStr(out, entry->key->chars, entry->key->length);
        fputc(':', out);
        jsonValue(out, entry->value);
    }
    fputs("}}\n", out);
    fflush(out);
}

void traceOutput(FILE* out, Value val) {
    fputs("{\"type\":\"out\",\"val\":", out);
    jsonValue(out, val);
    fputs("}\n", out);
    fflush(out);
}

void traceError(FILE* out, const char* msg) {
    fputs("{\"type\":\"error\",\"msg\":", out);
    jsonStr(out, msg, (int)strlen(msg));
    fputs("}\n", out);
    fflush(out);
}

void traceDone(FILE* out) {
    fputs("{\"type\":\"done\"}\n", out);
    fflush(out);
}
