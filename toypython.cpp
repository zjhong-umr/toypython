#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>
#include <cstdio>
#include <fstream>

using namespace llvm;

// define my getchar so that we can read from files and the terminal by modifying it
static FILE* inputFile, *outputFile;
char myGetchar() {
	return fgetc(inputFile);
}

/*
def func(x, y):
	return x+y
*/

/******************************************************************************************
   Lexer
******************************************************************************************/

// return 0-255 for unknown characters
enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_return = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
  tok_assign = -6,

  // main function
  tok_main = -7,
};

static std::string IdentifierStr; // Filled in if tok_identifier
static int NumVal;             // Filled in if tok_number

// gettok - Return the next token from standard input.
static int gettok() {
	static int LastChar = ' ';

	// Skip any whitespace.
	while (isspace(LastChar))
		LastChar = myGetchar();

	if (isalpha(LastChar)) { // [a-zA-Z] isalpha(ch) return true when ch is a character
		IdentifierStr = LastChar;
		while (isalnum(LastChar = myGetchar())) { // isalnum(ch) return true when ch is a character or a number
			IdentifierStr += LastChar;
		}

		if (IdentifierStr == "def") {
			return tok_def;
		}

		if (IdentifierStr == "main") {
			return tok_main;
		}

		if (IdentifierStr == "return") {
			return tok_return;
		}

		return tok_identifier;
	}

	if (LastChar == '=') { // assign
		LastChar = myGetchar();
		return tok_assign;
	}

	if (isdigit(LastChar) || LastChar == '.') { // [0-9]*(.[0-9]+)？ only think about positive real numbers and 
												// assume all the numbers are decimal (can be improved)
		std::string NumStr;
		do {
			NumStr += LastChar;
			LastChar = myGetchar();
		} while (isdigit(LastChar) || LastChar == '.');

		NumVal = strtod(NumStr.c_str(), 0);  // string to decimal
		return tok_number;
	}

	if (LastChar == '#') { // comment
		do {
			LastChar = myGetchar();
		} while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

		if (LastChar == EOF) {
			return tok_eof;
		}
	}

	if (LastChar == EOF) { // end of file
		return tok_eof;
	}

	// Otherwise, just return the character as its ascii value.
	int theChar = LastChar;
	LastChar = myGetchar();
	return theChar;
}

/******************************************************************************************
	Abstract Syntax Tree
******************************************************************************************/

namespace {

class BlockContext;

class ExprAST {
public:
	virtual ~ExprAST() = default;
	virtual Value *codegen(BlockContext&) = 0;
};

class NumberExprAST: public ExprAST {
private:
	int val;
public:
	NumberExprAST(int val): val(val) {	}
	Value *codegen(BlockContext&) override;
};

// for referencing a variable, like "x"
class VariableExprAST: public ExprAST {
private:
	std::string variableName;
public:
	VariableExprAST(const std::string &Name): variableName(Name)
	{	}

	Value *codegen(BlockContext&) override;
	std::string getName() const { return variableName; }
};

// x = 1
class AssignAST: public ExprAST {
private:
	std::unique_ptr<VariableExprAST> lhs;
	std::unique_ptr<ExprAST> rhs;
public:
	AssignAST(std::unique_ptr<VariableExprAST> lhs, std::unique_ptr<ExprAST> rhs):
		lhs(std::move(lhs)), rhs(std::move(rhs))
		{	}
	Value *codegen(BlockContext&) override;
};

class ReturnAST: public ExprAST {
private:
	std::unique_ptr<ExprAST> retVal;
public:
	ReturnAST(std::unique_ptr<ExprAST> retVal): retVal(std::move(retVal))
		{	}
	Value *codegen(BlockContext&) override;
};

class BlockContext {
public:
	std::map<std::string, Value*> locals;
	BasicBlock *currentBlock;
};

class Block {
private:
	std::vector<std::unique_ptr<ExprAST>> exprs;
	std::vector<std::string> args;
	BlockContext context;
public:
	Block() {	}
	void insertExpr(std::unique_ptr<ExprAST> expr) {
		exprs.push_back(std::move(expr));
	}
	void codegen();
};

}

/******************************************************************************************
	Parser
******************************************************************************************/
static int curTok;
static int getNextToken() { return curTok = gettok(); }

static std::unique_ptr<ExprAST> parseExpression();

// helper functions for error handling
std::unique_ptr<ExprAST> logError(const char *str) {
	fprintf(stderr, "Error: %s\n", str);
	return nullptr;
}

static std::unique_ptr<ExprAST> parseVariable() {
	auto result = llvm::make_unique<VariableExprAST>(IdentifierStr);
	getNextToken();
	return std::move(result);
}

// numberexpr ::= number
static std::unique_ptr<ExprAST> parseNumberExpr() {
	auto result = llvm::make_unique<NumberExprAST>(NumVal);
	getNextToken(); // consume the number;
	return std::move(result);
}

static std::unique_ptr<AssignAST> parseAssignExpr() {
	auto lhs = llvm::make_unique<VariableExprAST>(IdentifierStr);
	getNextToken(); // eat = 
	auto rhs = llvm::make_unique<NumberExprAST>(NumVal);
	getNextToken();
	return make_unique<AssignAST>(std::move(lhs), std::move(rhs));
}

static std::unique_ptr<ExprAST> parseExpression() {
	getNextToken();
	switch (curTok) {
		default: {
			return logError("unknown token when expecting an expression");
		}
		case tok_assign: {
			return parseAssignExpr();
		}
	}
}

static std::unique_ptr<ExprAST> parseReturnExpr() {
	getNextToken(); // consume return
	switch (curTok) {
		default: {
			return logError("unknown token when expecting an expression");
		}
		case tok_number: {
			return llvm::make_unique<ReturnAST>(parseNumberExpr());
		}
		case tok_identifier: {
			return llvm::make_unique<ReturnAST>(parseVariable());
		}
	}
}

static std::unique_ptr<ExprAST> parsePrimary() {
	switch (curTok) {
		default: {
			return logError("unknown token when expecting an expression");
		}
		case tok_number: {
			return parseNumberExpr();
		}
		case tok_identifier: {
			return parseExpression();
		}
		case tok_return: {
			return parseReturnExpr();
		}
	}
}

/******************************************************************************************
	code generation
******************************************************************************************/
static LLVMContext theContext;
static IRBuilder<> builder(theContext);
static std::unique_ptr<Module> theModule;
static std::map<std::string, Value *> nameValues;

static Block mainBlock;

Value *logErrorV(const char *str) {
	logError(str);
	return nullptr;
}

Value *NumberExprAST::codegen(BlockContext& context) {
	return ConstantInt::get(theContext, APInt(32,val));
}

Value *VariableExprAST::codegen(BlockContext& context) {
	IntegerType *int_type = Type::getInt32Ty(theContext);
	return new AllocaInst(int_type, variableName, context.currentBlock);
}

Value *AssignAST::codegen(BlockContext& context) {
	//IntegerType *int_type = Type::getInt64Ty(theContext);
	auto rhsValue = rhs->codegen(context);
	context.locals[lhs->getName()] = rhsValue;
	return new StoreInst(rhsValue, lhs->codegen(context),
		false, context.currentBlock);
}

Value *ReturnAST::codegen(BlockContext& context) {
	return builder.CreateRet(retVal->codegen(context));
}

void Block::codegen() {
	std::vector<Type *> ints(0, Type::getInt32Ty(theContext));
	FunctionType *FT = FunctionType::get(Type::getInt32Ty(theContext), ints, false);
	Function *theFunction = Function::Create(FT, Function::ExternalLinkage, "main", theModule.get());

	context.currentBlock = BasicBlock::Create(theContext, "entry", theFunction);
	builder.SetInsertPoint(context.currentBlock);

	verifyFunction(*theFunction);
	for (auto&& expr: exprs) {
		auto *fnIR = expr->codegen(context);
		fnIR->print(errs());
	}
}

/******************************************************************************************
	top-level parsing and JIT driver
******************************************************************************************/

// static void handleDefinition();

static void handleTopLevelExpression() {
	auto temp = parsePrimary();
	mainBlock.insertExpr(std::move(temp));
}

static void mainLoop() {
	while (true) {
		switch (curTok) {
			case tok_eof: {
				return;
			}
			case ';': {
				getNextToken();
				break;
			}
			default: {
				handleTopLevelExpression();
				break;
			}
		}
	}
}

int main(int argc, char *argv[]) {
	std::cout<<"I am fine, thank you. And you?\n";
	inputFile = fopen(argv[1], "r");

	getNextToken();
	// Make the module, which holds all the code.
	theModule = llvm::make_unique<Module>(argv[1], theContext);

	mainLoop();
	mainBlock.codegen();

	std::string str;
	raw_string_ostream OS(str);
	OS << *theModule;
	OS.flush();	

	//std::cout<<theModule->getFunction("main");

	std::string outputFile = argv[1];
	outputFile += ".Output";
	std::ofstream fout(outputFile, std::ofstream::out);
	fout<<str;

	return 0;
}