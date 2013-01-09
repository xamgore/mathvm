/*
 * Interpreter.h
 *
 *  Created on: Dec 17, 2012
 *      Author: yarik
 */

#ifndef INTERPRETER_H_
#define INTERPRETER_H_

#include <mathvm.h>
#include <ast.h>
#include <iostream>
#include <string>
#include <stack>
#include <fstream>
#include <stdlib.h>

using std::string;
using std::cin;
using std::cout;
using std::ostream;
using std::fstream;
using std::stack;

using namespace mathvm;



union StackVar {
	uint16_t strId;
	int64_t i;
	double d;
};


struct BytecodeConstants {

	static const uint32_t int_size = sizeof(int64_t);
	static const uint32_t double_size = sizeof(double);
	static const uint32_t insn_size = 1;
    static const uint32_t string_id_size = 2;
    static const uint32_t ctx_id_size = 2;
    static const uint32_t id_in_ctx_size = 2;

};


class FunctionContext {
public:
	FunctionContext(uint16_t functionId, FunctionContext* parent = 0);

	int64_t readInt(uint16_t context, uint16_t id);
	double readDouble(uint16_t context, uint16_t id);

	void storeDouble(uint16_t context, uint16_t id, double value);
	void storeInt(uint16_t context, uint16_t id, int64_t value);

	uint16_t getId();

private:

	const uint16_t _id;
	FunctionContext* const _parent;
    map<uint16_t, StackVar> vars;


    int64_t readIntFromCurrCTX(uint16_t id);
    double readDoubleFromCurrCTX(uint16_t id);
    void storeDoubleInCurrCTX(uint16_t id, double value);
    void storeIntInCurrCTX(uint16_t id, int64_t value);
};




class Interpreter {



public:
	Interpreter(ostream& out);
	virtual ~Interpreter();

    void execute(Code* code);


private:
	ostream& _out;



    Code* _code;

	Bytecode* _bc;
	uint32_t _insPtr;


	stack<StackVar> _stack;
	stack<VarType> _typeStack;

	void popVar() {
		if ((_stack.size() == 0) || (_typeStack.size() == 0))
			assert(false);

        _stack.pop();
        _typeStack.pop();
	}

	void pushVar(VarType type, StackVar var) {
        _typeStack.push(type);
        _stack.push(var);
	}



	stack<FunctionContext*> _contexts;


	void pushContext(uint16_t id) {
		FunctionContext* parent = (_contexts.size() > 0) ? _contexts.top() : 0;
		_contexts.push(new FunctionContext(id, parent));
	}

	void popContext() {
		if (_contexts.size() == 0) {
			assert(false);
		}
		FunctionContext* top = _contexts.top();
		_contexts.pop();
		delete top;
		return;
	}

	FunctionContext* topContext() {
        if (_contexts.size() == 0) {
        	assert(false);
        }
        return _contexts.top();
	}


    void movePtrInsnDown();
    void movePtrIntDown();
    void movePtrDoubleDown();
    void movePtr2Bytes();

    Instruction nextInsn();
    double getNextDouble();
    int64_t getNextInt();
    uint16_t getNext2Bytes();


	void loadDouble();
	void loadString();
	void loadInt();

	void printInt();
	void printDouble();
	void printString();


	/*
	 * Operations with variables stack
	 */
	void pushInt(int64_t);
	void pushDouble(double);
	int64_t popInt();
	double popDouble();


	void storeCtxDouble();
	void storeCtxInt();

	void loadCtxDouble();
	void loadCtxInt();

	void swap();

	void subInts();
	void subDoubles();

	void loadFunParamsInCtx(BytecodeFunction* fun);
	void call();



	void checkTypesTOS(VarType type) {
		if (_typeStack.top() != type) {
		   	cout << "NOT INT ON TOS" << endl;
		   	assert(false);
		}

		if ((_typeStack.size() == 0) || (_stack.size() == 0)) {
		   	cout << "EMPTY STACK" << endl;
		   	assert(false);
		}
	}



};

#endif /* INTERPRETER_H_ */