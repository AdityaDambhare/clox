#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "chunk.h"
#include "object.h"
#define UINT16_COUNT UINT16_MAX+1
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
PREC_CALL,        // . ()
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
}Local;


typedef struct{
    Local locals[UINT16_COUNT];
    int localCount;
    int scopeDepth;
}Compiler;


Parser parser;
Chunk* compilingChunk;
static Compiler current;
static Chunk* currentChunk(){
    return compilingChunk;
}

static int currentLoopStart = -1;
static int currentLoopScope = 0;
static int currentExitJump = -1;

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

static void initCompiler(Compiler* compiler){
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
}

static void endCompiler(){
    emitReturn();
#ifdef DEBUG_PRINT_CODE
    if(!parser.hadError){
        dissassembleChunk(currentChunk(),"code");
    }
#endif
}

static void beginScope(){
    current.scopeDepth++;
}

static void endScope(){
    current.scopeDepth--;
    while(current.localCount>0&&current.locals[current.localCount-1].depth>current.scopeDepth){
        emitByte(OP_POP);
        current.localCount--;
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
static void namedVariable(Token token,bool canAssign){
    int  arg = resolveLocal(&current,&token);
    if(arg!=-1){
        localVariable(token,canAssign,arg);
        return;
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

ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE}, 
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     comma,   PREC_COMMA},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
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
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     or_,   PREC_OR},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
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

static void addLocal(Token name){
    if(current.localCount == UINT16_COUNT){
        error("Too many local variables in function.");
        return;
    }
    Local* local = &current.locals[current.localCount++];
    local->name = name;
    local->depth = -1;
}
static void declareVariable(){
    if(current.scopeDepth == 0) return;
    Token* name = &parser.previous;
    for(int i = current.localCount-1;i>=0;i--){
        Local* local = &current.locals[i];
        if(local->depth != -1 && local->depth < current.scopeDepth){
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
  if(current.scopeDepth>0) return 0;
  return identifierConstant(&parser.previous);
}

static void markInitialized(){
    current.locals[current.localCount-1].depth = current.scopeDepth;
}

static void defineVariable(int global) {
  if(current.scopeDepth >  0){
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
    int sorroundingLoopStart = currentLoopStart;
    int sorroundingexitJump = currentExitJump;
    currentLoopStart = currentChunk()->count;
    consume(TOKEN_LEFT_PAREN,"Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN,"Expect ')' after condition.");
    currentExitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
    statement();
    emitLoop(currentLoopStart);
    patchJump(currentExitJump);
    emitByte(OP_POP);
    currentExitJump = sorroundingexitJump;
    currentLoopStart = sorroundingLoopStart;
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
    int sorroundingLoopStart = currentLoopStart;
    int sorroundingLoopScope = currentLoopScope;
    int sorroundingExitJump = currentExitJump;
    currentLoopStart = currentChunk()->count;
    currentLoopScope = current.scopeDepth;
    if(!match(TOKEN_SEMICOLON)){
        expression();
        consume(TOKEN_SEMICOLON,"Expect ';' after loop condition.");
        currentExitJump = emitJump(OP_JUMP_IF_FALSE);
        emitByte(OP_POP);
    }
    else{
        emitByte(OP_TRUE);
        currentExitJump = emitJump(OP_JUMP_IF_FALSE);
    }
    if (!match(TOKEN_RIGHT_PAREN)){
        int bodyJump = emitJump(OP_JUMP);
        int incrementStart = currentChunk()->count;
        expression();
        emitByte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emitLoop(currentLoopStart);
        currentLoopStart = incrementStart;
        patchJump(bodyJump);
    }
    statement();
    emitLoop(currentLoopStart);

    patchJump(currentExitJump);
    emitByte(OP_POP);
    currentExitJump = sorroundingExitJump;
    currentLoopScope = sorroundingLoopScope;
    currentLoopStart = sorroundingLoopStart;
    endScope();
}

static void continueStatement(){
    if(currentLoopStart == -1){
        error("Can't use 'continue' outside of a loop.");
    }

    consume(TOKEN_SEMICOLON,"Expect ';' after 'continue'.");

    for(int i = current.localCount-1;
        i>=0 && current.locals[i].depth>currentLoopScope;
        i--)
    {
        emitByte(OP_POP);
    }
    emitLoop(currentLoopStart);
}

static void breakStatement(){
    if(currentExitJump == -1){
        error("Can't use 'break' outside of a loop.");
    }
    consume(TOKEN_SEMICOLON,"Expect ';' after 'break'.");
    for(int i = current.localCount-1;
        i>=0 && current.locals[i].depth>currentLoopScope;
        i--)
    {
        emitByte(OP_POP);
    }
    emitByte(OP_FALSE);
    emitLoop(currentExitJump-1);
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
    if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
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
    else{
        expressionStatement();
    }
}
static void comma(bool canAssign){
    emitByte(OP_POP);
    parsePrecedence(PREC_COMMA);
}
bool compile(const char* source,Chunk* chunk){
    initScanner(source);
    initCompiler(&current);
    compilingChunk = chunk;
    parser.hadError = false;
    parser.panicMode = false;
    advance();
    while(!match(TOKEN_EOF)){
        declaration();
    }
    endCompiler();
    return !parser.hadError;
}