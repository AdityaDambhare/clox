#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "chunk.h"
#include "object.h"
#include "memory.h"
#define UINT16_COUNT UINT16_MAX+1
#define UINT8_COUNT UINT8_MAX+1
#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif
typedef struct{
    Token current;
    Token previous;
    bool hadError;
    bool panicMode; 
} Parser;

typedef enum{
PREC_NONE,
PREC_COMMA,       //,
PREC_ASSIGNMENT,  //=
PREC_TERNARY,     //?:
PREC_OR,          // or
PREC_AND,         // and
PREC_EQUALITY,    // == !=
PREC_COMPARISON,  // < > <= >=
PREC_TERM,        // + -
PREC_FACTOR,      // * /
PREC_UNARY,       // ! -
PREC_POWER,       // ^
PREC_CALL,        // . () []
PREC_PRIMARY
} Precedence;
typedef void (*ParseFn)(bool canAssign);
typedef struct{
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
}ParseRule;

typedef struct {
    Token name;
    int depth;
    bool isCaptured;
}Local;

typedef struct {
  uint16_t index;
  bool isLocal;
} Upvalue;

typedef enum{
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
    TYPE_INITIALIZER,
    TYPE_GETTER,
    TYPE_EXPRESSION
}FunctionType;


typedef struct Compiler{
    struct Compiler* enclosing;
    ObjFunction* function;
    FunctionType type;
    Local *locals;
    int localCount;
    Upvalue *upvalues;
    int scopeDepth;
    int currentLoopStart;
    int currentLoopScope;
    int currentExitJump;

}Compiler;

typedef struct ClassCompiler{
    struct ClassCompiler* enclosing;
    bool hasSuperclass;
}ClassCompiler;



Parser parser;
static Compiler* current = NULL;
static ClassCompiler* currentClass = NULL;


static Chunk* currentChunk(){
    return &current->function->chunk;
}


static void errorAt(Token* token,const char* message){
    if(parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr,"[line %d] Error",token->line);
    if(token->type==TOKEN_EOF){
        fprintf(stderr," at end");
    }else if(token->type==TOKEN_ERROR){}
    else{
        fprintf(stderr," at '%.*s'",token->length,token->start);
    }
    fprintf(stderr,": %s\n",message);
    parser.hadError = true;
}

static void errorAtCurrent(const char* message){
    errorAt(&parser.current,message);
}

static void error(const char* message){
    errorAt(&parser.previous,message);
}

static void advance(){
    parser.previous = parser.current;
    for(;;){//skip all the error tokens whislt reporting them
        parser.current = scanToken();
        if(parser.current.type != TOKEN_ERROR) break;
        errorAtCurrent(parser.current.start);
    }
}
static void consume(TokenType type,const char* message){
    if(parser.current.type == type){
        advance();
        return;
    }
    errorAtCurrent(message);
}


static bool check(TokenType type){
    return parser.current.type == type;
}

static bool match(TokenType type){
    if(!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte){
    writeChunk(currentChunk(),byte,parser.previous.line);
}
static void emitBytes(uint8_t byte1,uint8_t byte2){
    emitByte(byte1);
    emitByte(byte2);
}

static void emitLoop(int loopStart) {
  emitByte(OP_LOOP);

  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) error("Loop body too large.");

  emitByte((offset >> 8) & 0xff);
  emitByte(offset & 0xff);
}

static int emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitByte(0xff);
  emitByte(0xff);
  return currentChunk()->count - 2;
}

static void emitReturn(){
    if(current->type==TYPE_INITIALIZER){
        emitByte(OP_GET_LOCAL);
        emitBytes(0,0); 
    }
    else{
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

static void emitConstant(Value value){
    if(!writeConstant(currentChunk(),value,parser.previous.line)){
        error("Too many constants in one chunk.");
    }
}

static void patchJump(int offset) {
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) {
    error("Too much code to jump over.");
  }

  currentChunk()->code[offset] = (jump >> 8) & 0xff;
  currentChunk()->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Compiler* compiler,FunctionType type){
    compiler->enclosing = current;
    compiler->function = NULL;
    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->currentLoopStart = -1;
    compiler->currentLoopScope = -1;
    compiler->currentExitJump = -1;
    compiler->function = newFunction();
    compiler->locals = (Local*)malloc(sizeof(Local)*UINT16_COUNT);
    compiler->upvalues = (Upvalue*)malloc(sizeof(Upvalue)*UINT16_COUNT);
    // implicitly giving the first slot to the vm for internal use
    current = compiler;
    if(type!=TYPE_SCRIPT&&type!=TYPE_EXPRESSION){
        current->function->name = copyString(parser.previous.start,parser.previous.length);
    }
    if(type==TYPE_EXPRESSION){
        current->function->name = copyString("",1);
    }

    Local* local = &current->locals[current->localCount++];
    local->depth = 0;
    local->name.start = "";
    local->name.length = 0;
    local->isCaptured = false;
    if (type != TYPE_FUNCTION&&type!=TYPE_SCRIPT&&type!=TYPE_EXPRESSION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static ObjFunction* endCompiler(){
    emitReturn();
    ObjFunction* function = current->function;
#ifdef DEBUG_PRINT_CODE
    if(!parser.hadError){
        const char* name = function->name != NULL ? function->name->chars : "<script>";
        dissassembleChunk(currentChunk(), name);
    }
#endif
    free(current->locals);
   // free(current->upvalues);
    current = current->enclosing;
    return function;
}

static void beginScope(){
    current->scopeDepth++;
}

static void endScope(){
    current->scopeDepth--;
    while(current->localCount>0&current->locals[current->localCount-1].depth>current->scopeDepth){
        if(current->locals[current->localCount-1].isCaptured){
            emitByte(OP_CLOSE_UPVALUE);
        }
        else{
            emitByte(OP_POP);
        }
        current->localCount--;
    }//pops all the local variables that are out of scope
}

static void expression();
static void declaration();
static void statement();
static int identifierConstant(Token* name);
static void comma(bool canAssign);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);
static int resolveLocal(Compiler* compiler,Token* name);
static uint8_t argumentList();
static int resolveUpvalue(Compiler* compiler, Token* name);
static void function(FunctionType type);

static void binary(bool canAssign){
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    parsePrecedence((Precedence)(rule->precedence+1));
    switch (operatorType)
    {
        case TOKEN_PLUS: emitByte(OP_ADD); break;
        case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
        case TOKEN_BANG_EQUAL : emitBytes(OP_EQUAL,OP_NOT); break;
        case TOKEN_EQUAL_EQUAL : emitByte(OP_EQUAL); break;
        case TOKEN_GREATER : emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL : emitBytes(OP_LESS,OP_NOT); break;
        case TOKEN_LESS : emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL : emitBytes(OP_GREATER,OP_NOT); break;
        case TOKEN_POWER : emitByte(OP_POWER); break;
        default:return;
    }
}

static void call(bool canAssign) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

static void dot(bool canAssign){
    consume(TOKEN_IDENTIFIER,"Expect property name after '.'.");
    int name = identifierConstant(&parser.previous);
    if(canAssign&&match(TOKEN_EQUAL)){
        expression();
        emitByte(OP_SET_PROPERTY);
        emitBytes((uint8_t)(name >> 8), (uint8_t)(name & 0xff));
    } else if (match(TOKEN_LEFT_PAREN)){
        uint8_t argCount = argumentList();
        emitByte(OP_INVOKE);
        emitBytes((uint8_t)(name>>8),(uint8_t)(name&0xff));
        emitByte(argCount);
    }
    else{
        emitByte(OP_GET_PROPERTY);
        emitBytes((uint8_t)(name >> 8), (uint8_t)(name & 0xff));
    }
}

static void conditional(bool canAssign){
    int elseJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    parsePrecedence(PREC_TERNARY);
    consume(TOKEN_COLON,"Expect ':' after ?: expression.");
    int endJump = emitJump(OP_JUMP);
    patchJump(elseJump);
    emitByte(OP_POP);
    parsePrecedence(PREC_TERNARY);
    patchJump(endJump);
}

static void literal(bool canAssign) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_NIL: emitByte(OP_NIL); break;
    case TOKEN_TRUE: emitByte(OP_TRUE); break;
    default: return; 
  }
}
static void grouping(bool canAssign){
    expression();
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after expression.");
}
static void number(bool canAssign){
    double value = strtod(parser.previous.start,NULL);
    emitConstant(NUMBER_VAL(value));
}
//we reuse jumps for logical operators
static void or_(bool canAssign) {\
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitByte(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static void and_(bool canAssign) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitByte(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

static void string(bool canAssign){
    emitConstant(OBJ_VAL(copyString(parser.previous.start+1,parser.previous.length-2)));
}
static void localVariable(Token token,bool canAssign,int arg){
    if(canAssign&&match(TOKEN_EQUAL)){
        expression();
        emitByte(OP_SET_LOCAL);
        emitBytes((uint8_t)(arg>>8),(uint8_t)(arg&0xff));
    }
    else{
        emitByte(OP_GET_LOCAL);
        emitBytes((uint8_t)(arg>>8),(uint8_t)(arg&0xff));
    }
}
static void UpValue(Token token,bool canAssign,uint16_t arg){
    if(canAssign&&match(TOKEN_EQUAL)){
        expression();
        emitByte(OP_SET_UPVALUE);
        emitBytes((uint8_t)(arg>>8),(uint8_t)(arg&0xff));
    }
    else{
        emitByte(OP_GET_UPVALUE);
        emitBytes((uint8_t)(arg>>8),(uint8_t)(arg&0xff));
    }
}
static void namedVariable(Token token,bool canAssign){
    int  arg = resolveLocal(current,&token);
    if(arg!=-1){
        localVariable(token,canAssign,arg);
        return;
    } else if ((arg = resolveUpvalue(current,&token))!=-1) {
        UpValue(token,canAssign,(uint16_t)arg);
        return ;
    }
    else{
        arg = (uint16_t)identifierConstant(&token);
    }
    if(canAssign&&match(TOKEN_EQUAL)) goto SET;
    if( arg < 256){
        emitBytes(OP_GET_GLOBAL,(uint8_t)arg);
    }
    else{
        emitByte(OP_GET_GLOBAL_LONG);
        emitBytes((uint8_t)(arg >> 8), (uint8_t)(arg & 0xff));
    }
    return;
    SET:
    parsePrecedence(PREC_ASSIGNMENT);
    if(arg<256){
        emitBytes(OP_SET_GLOBAL,(uint8_t)arg);
    }
    else{
        emitByte(OP_SET_GLOBAL_LONG);
        emitBytes((uint8_t)(arg >> 8), (uint8_t)(arg & 0xff));
    }
}
static void variable(bool canAssign){
    namedVariable(parser.previous,canAssign);
}

static Token syntheticToken(const char* name){
    Token token;
    token.start = name;
    token.length = (int)strlen(name);
    token.line = parser.current.line;
    return token;
}

static void unary(bool canAssign){
    TokenType operatorType = parser.previous.type;
    parsePrecedence(PREC_UNARY);
    switch (operatorType)
    {
    case TOKEN_BANG: emitByte(OP_NOT); break;
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default:return;
    }
}

static void this_(bool canAssign){
    if(currentClass == NULL){
        error("Can't use 'this' outside of a class.");
        return;
    }
    variable(false);
}

static void super_(bool canAssign){
    if(currentClass==NULL){
        error("Can't use 'super' outside of a class.");
    }
    else if(!currentClass->hasSuperclass){
        error("Can't use 'super' in a class with no superclass.");
    }
    consume(TOKEN_DOT,"Expect '.' after 'super'");
    consume(TOKEN_IDENTIFIER,"Expect superclass method name"); 
    uint16_t name = identifierConstant(&parser.previous);
    namedVariable(syntheticToken("this"),false);
    if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    namedVariable(syntheticToken("super"), false);
    emitByte(OP_INVOKE_SUPER);
    emitBytes((uint8_t)(name >> 8), (uint8_t)(name & 0xff));
    emitByte(argCount);
  } else {
    namedVariable(syntheticToken("super"), false);
    emitByte(OP_GET_SUPER);
    emitBytes((uint8_t)(name >> 8), (uint8_t)(name & 0xff));
  }
}

static void functionExpression(bool canAssign){
    function(TYPE_EXPRESSION);
}

static void list(bool canAssign){
    uint16_t length = 0;
    while(!check(TOKEN_RIGHT_SQUARE)){
        parsePrecedence(PREC_ASSIGNMENT);
        if(length++>=UINT16_COUNT){
            error("Too many elements in list.");
        }
        if(!match(TOKEN_COMMA)) break;
    }
    consume(TOKEN_RIGHT_SQUARE,"Expect ']' after list declaration.");
    emitByte(OP_MAKE_LIST);
    emitBytes((uint8_t)(length >> 8), (uint8_t)(length & 0xff));
}

static void subscript(bool canAssign){
    expression();
    consume(TOKEN_RIGHT_SQUARE,"Expect ']' after subscript.");
    if(canAssign&&match(TOKEN_EQUAL)){
        expression();
        emitByte(OP_SET_ELEMENT);
    }
    else{
        emitByte(OP_GET_ELEMENT);
    }
}

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, call,   PREC_CALL},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_SQUARE]   = {list,     subscript,   PREC_CALL},
  [TOKEN_RIGHT_SQUARE]  = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     comma,   PREC_COMMA},
  [TOKEN_DOT]           = {NULL,     dot,   PREC_CALL},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_POWER]         = {NULL,     binary, PREC_POWER},
  [TOKEN_COLON]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_QUESTION]      = {NULL,     conditional,   PREC_TERNARY},
  [TOKEN_BANG]          = {unary,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {variable,     NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     and_,   PREC_AND},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,     NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {functionExpression,     NULL,   PREC_PRIMARY},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     or_,   PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {super_,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {this_,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,     NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};

static void parsePrecedence(Precedence precedence){
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

    while(precedence <= getRule(parser.current.type)->precedence){
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
    }   

    if(canAssign && match(TOKEN_EQUAL)){
        error("Invalid assignment target.");
    }
}

static int identifierConstant(Token* name) {
  for(int i = 0;i<currentChunk()->constants.count;i++){
      if(IS_OBJ(currentChunk()->constants.values[i])
        &&
        OBJ_TYPE(currentChunk()->constants.values[i])==OBJ_STRING)
     {
          ObjString* string = AS_STRING(currentChunk()->constants.values[i]);
          if(string->length == name->length && memcmp(string->chars,name->start,name->length)==0){
              return i;
          }
      }
  }
  return addConstant(currentChunk(), OBJ_VAL(copyString(name->start, name->length)));
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if(local->depth==-1){
        error("Can't read variable in its own initializer");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(Compiler* compiler, uint16_t index,
                      bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;
  for(int i = 0;i<upvalueCount;i++){
      Upvalue* upvalue = &compiler->upvalues[i];
      if(upvalue->index == index && upvalue->isLocal == isLocal){
          return i;
      }
  }
  if(upvalueCount == UINT16_COUNT){
      error("Too many closure variables in function.");
      return 0;
  }
  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
  if (compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint16_t)local, true);
  }
  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(compiler, (uint16_t)upvalue, false);
  }
  return -1;
}

static void addLocal(Token name){
    if(current->localCount == UINT16_COUNT){
        error("Too many local variables in function.");
        return;
    }
    Local* local = &current->locals[current->localCount++];
    local->name = name;
    local->depth = -1;
    local->isCaptured = false;
}
static void declareVariable(){
    if(current->scopeDepth == 0) return;
    Token* name = &parser.previous;
    for(int i = current->localCount-1;i>=0;i--){
        Local* local = &current->locals[i];
        if(local->depth != -1 && local->depth < current->scopeDepth){
            break;
        }
        if(identifiersEqual(name,&local->name)){
            error("Variable with this name already declared in this scope.");
        }
    }
    addLocal(*name);
}
static int parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);
  declareVariable();//wont run in global scope
  if(current->scopeDepth>0) return 0;
  return identifierConstant(&parser.previous);
}

static void markInitialized(){
    if (current->scopeDepth == 0) return;
    current->locals[current->localCount-1].depth = current->scopeDepth;
}

static void defineVariable(int global) {
  if(current->scopeDepth >  0){
    markInitialized();
    return;
  }
  if(global<256){
      emitBytes(OP_DEFINE_GLOBAL, (uint8_t)global);
  }
  else{
      emitByte(OP_DEFINE_GLOBAL_LONG);
      emitBytes((uint8_t)(global >> 8), (uint8_t)(global & 0xff));
  }
}

static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      parsePrecedence(PREC_ASSIGNMENT);//don't want comma operator to mess with parsing arguments
      if (argCount == 255) {
        error("Can't have more than 255 arguments.");
      }
      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}


static ParseRule* getRule(TokenType type){
    return &rules[type];
}

static void expression(){
    parsePrecedence(PREC_COMMA);
}

static void block(){
    while(!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF)){
        declaration();
    }
    consume(TOKEN_RIGHT_BRACE,"Expect '}' after block.");
}

static void function(FunctionType type){
    Compiler compiler;
    initCompiler(&compiler,type);
    beginScope();
    if(type==TYPE_GETTER){
        current->function->arity = -1;//getters have negative arity
        goto GETTER_LABEL;
    }
    consume(TOKEN_LEFT_PAREN,"Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
        current->function->arity++;
        if (current->function->arity > 255) {
            errorAtCurrent("Can't have more than 255 parameters.");
        }
        int constant = parseVariable("Expect parameter name.");
        defineVariable(constant);
        } while (match(TOKEN_COMMA));
  }
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after parameters.");
    GETTER_LABEL: //forgive me, the intrusive thoughts have won
    consume(TOKEN_LEFT_BRACE,"Expect '{' before function body.");
    block();
    ObjFunction* function = endCompiler();
    int func = addConstant(currentChunk(),OBJ_VAL(function)); // you don't know just HOW important the order of these two lines is
    emitByte(OP_CLOSURE);//with DEBUG_STRESS_GC enabled, emitByte() will call the gc, free the function and the function will be deallocated before it is added to the constants array
    //i spent 2 hours trying to find the line causing this bug
    if(func>UINT16_COUNT-1){
        error("Too many constants in one chunk.");
    }
    emitBytes((uint8_t)(func >> 8), (uint8_t)(func & 0xff));
    for (int i = 0; i < function->upvalueCount; i++){
        emitByte((uint8_t)(compiler.upvalues[i].isLocal ? 1 : 0));
        emitBytes((uint8_t)(compiler.upvalues[i].index>>8),(uint8_t)(compiler.upvalues[i].index&0xff));
    }
}

static void method(){
    consume(TOKEN_IDENTIFIER,"Expect method name.");
    uint16_t constant = identifierConstant(&parser.previous);
    FunctionType type = TYPE_METHOD;
    if(!check(TOKEN_LEFT_PAREN)){
        type = TYPE_GETTER;
    }
    if (parser.previous.length == 4 &&
    memcmp(parser.previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
    }
    function(type);
    emitByte(OP_METHOD);
    emitBytes((uint8_t)(constant >> 8), (uint8_t)(constant & 0xff));
}
static void classDeclaration(){
    consume(TOKEN_IDENTIFIER,"Expect class name.");
    Token name = parser.previous;
    uint16_t nameConstant = identifierConstant(&parser.previous);
    declareVariable();
    emitByte(OP_CLASS);
    emitBytes((uint8_t)(nameConstant >> 8), (uint8_t)(nameConstant & 0xff));
    defineVariable(nameConstant);
    ClassCompiler classCompiler;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = currentClass;
    currentClass = &classCompiler;
    if(match(TOKEN_LESS)){
        consume(TOKEN_IDENTIFIER,"Expect superclass name.");
        variable(false);
        if(identifiersEqual(&name,&parser.previous)){
            error("A class can't inherit from itself.");
        }
        beginScope();
        addLocal(syntheticToken("super"));
        defineVariable(0);
        namedVariable(name,false);
        emitByte(OP_INHERIT);
        currentClass->hasSuperclass = true;
    }
    namedVariable(name,false);//load variable onto the stack
    consume(TOKEN_LEFT_BRACE,"Expect '{' before class body.");
    while(!check(TOKEN_RIGHT_BRACE)&&!check(TOKEN_EOF)){
        method();
    }
    consume(TOKEN_RIGHT_BRACE,"Expect '}' after class body.");
    emitByte(OP_POP);
    if(classCompiler.hasSuperclass){
        endScope();
    }
    currentClass = currentClass->enclosing;
}

static void functionDeclaration(){
    if(check(TOKEN_IDENTIFIER)){
    int global = parseVariable("Expect function name");
    markInitialized();
    function(TYPE_FUNCTION);
    defineVariable(global);
    }
    else{
        function(TYPE_EXPRESSION);
        emitByte(OP_POP);
    }
}

static void varDeclaration() {
  int global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    parsePrecedence(PREC_ASSIGNMENT);
  } else {
    emitByte(OP_NIL);
  }
  consume(TOKEN_SEMICOLON,"Expect ';' after variable declaration.");

  defineVariable(global);
}

static void expressionStatement(){
    expression();
    consume(TOKEN_SEMICOLON,"Expect ';' after expression.");
    emitByte(OP_POP);
}
static void if_statement(){
    consume(TOKEN_LEFT_PAREN,"Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after condition");
    int thenJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);//pop the condtition value when truthy
    statement();
    int elseJump = emitJump(OP_JUMP);//jump over else if truthy
    patchJump(thenJump);
    emitByte(OP_POP);//pop the condition value when falsey
    if(match(TOKEN_ELSE)){
        statement();
    }
    patchJump(elseJump);
}

static void whileStatement(){
    int sorroundingLoopStart = current->currentLoopStart;
    int sorroundingexitJump = current->currentExitJump;
    current->currentLoopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN,"Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after condition.");
    current->currentExitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(current->currentLoopStart);
    patchJump(current->currentExitJump);
    emitByte(OP_POP);
    current->currentExitJump = sorroundingexitJump;
    current->currentLoopStart = sorroundingLoopStart;
}

static void forStatement(){
  
    beginScope();
    consume(TOKEN_LEFT_PAREN,"Expect '(' after 'for'.");
    if(match(TOKEN_SEMICOLON)){
        //no initializer
    }
    else if(match(TOKEN_VAR)){
        varDeclaration();
    }
    else{
        expressionStatement();
    }
    int sorroundingLoopStart = current->currentLoopStart;
    int sorroundingLoopScope = current->currentLoopScope;
    int sorroundingExitJump = current->currentExitJump;
    current->currentLoopStart = currentChunk()->count;
    current->currentLoopScope = current->scopeDepth;
    if(!match(TOKEN_SEMICOLON)){
        expression();
        consume(TOKEN_SEMICOLON,"Expect ';' after loop condition.");
        current->currentExitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }
    else{
        emitByte(OP_TRUE);
        current->currentExitJump = emitJump(OP_JUMP_IF_FALSE);
    }
    if (!match(TOKEN_RIGHT_PAREN)){
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emitLoop(current->currentLoopStart);
        current->currentLoopStart = incrementStart;
        patchJump(bodyJump);
    }
    statement();
    emitLoop(current->currentLoopStart);

    patchJump(current->currentExitJump);
    emitByte(OP_POP);
    current->currentExitJump = sorroundingExitJump;
    current->currentLoopScope = sorroundingLoopScope;
    current->currentLoopStart = sorroundingLoopStart;
    endScope();
}

static void continueStatement(){
    if(current->currentLoopStart == -1){
        error("Can't use 'continue' outside of a loop.");
    }

    consume(TOKEN_SEMICOLON,"Expect ';' after 'continue'.");

    for(int i = current->localCount-1;
        i>=0 && current->locals[i].depth>current->currentLoopScope;
        i--)
    {
        emitByte(OP_POP);
    }
    emitLoop(current->currentLoopStart);
}

static void breakStatement(){
    if(current->currentExitJump == -1){
        error("Can't use 'break' outside of a loop.");
    }
    consume(TOKEN_SEMICOLON,"Expect ';' after 'break'.");
    for(int i = current->localCount-1;
        i>=0 && current->locals[i].depth>current->currentLoopScope;
        i--)
    {
        emitByte(OP_POP);
    }
    emitByte(OP_FALSE);
    emitLoop(current->currentExitJump-1);
}

static void returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }
  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else if(current->type == TYPE_INITIALIZER){
    error("Can't return a value from an initializer.");
  }else {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(OP_RETURN);
  }
}

static void printStatement(){
    expression();
    consume(TOKEN_SEMICOLON,"Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void synchronize(){
    parser.panicMode = false;
    while(parser.current.type!=TOKEN_EOF){
        if(parser.previous.type == TOKEN_SEMICOLON) return;
        switch(parser.current.type){
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
            return;
        }
        advance();
    }
}

static void declaration(){
    if(match(TOKEN_CLASS)){
        classDeclaration();
    }
    else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else if(match(TOKEN_FUN)){
    functionDeclaration();
  }else {
    statement();
  }
    if(parser.panicMode) synchronize();
}
static void statement(){
    if(match(TOKEN_PRINT)){
        printStatement();
    }
    else if(match(TOKEN_IF)){
        if_statement();
    }
    else if(match(TOKEN_WHILE)){
        whileStatement();
    }
    else if(match(TOKEN_FOR)){
        forStatement();
    }
    else if(match(TOKEN_LEFT_BRACE)){
        
        beginScope();
        block();
        endScope();
    } 
    else if (match(TOKEN_CONTINUE)){
        continueStatement();
    }
    else if (match(TOKEN_BREAK)){
        breakStatement();
    }
    else if (match(TOKEN_RETURN)){
        returnStatement();
    }
    else{
        expressionStatement();
    }
}
static void comma(bool canAssign){
    emitByte(OP_POP);
    parsePrecedence(PREC_COMMA);
}
ObjFunction* compile(const char* source){
    initScanner(source);
    Compiler compiler;
    initCompiler(&compiler,TYPE_SCRIPT);
    parser.hadError = false;
    parser.panicMode = false;
    advance();
    while(!match(TOKEN_EOF)){
        declaration();
    }
    ObjFunction* function = endCompiler();
    return parser.hadError ? NULL : function;
}

void markCompilerRoots(){
    Compiler* compiler = current;
    while(compiler!=NULL){
        markObject((Obj*)compiler->function);
        compiler = compiler->enclosing;
    }
}