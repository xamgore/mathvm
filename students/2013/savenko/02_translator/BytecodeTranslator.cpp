#include <mathvm.h>
#include <visitors.h>
#include <parser.h>
#include <ast.h>

#include <stdexcept>
#include <memory>

#include "InterpreterCodeImpl.h"
#include "DebugMacros.h"

namespace mathvm {

uint16_t ID_MAX = 0xFFFF;

class BytecodeScope {
  typedef std::map<std::string, uint16_t> FunctionMap;
  typedef std::map<std::string, uint16_t> VarMap;
public:
  typedef std::pair<uint16_t, uint16_t>   VarId; // (scopeId, varId);

public:
  BytecodeScope(Code* code) : _id(0), _parent(0), _code(code), _all_scopes(new std::vector<BytecodeScope *>) {
    _all_scopes->push_back(this);
  }
  
  uint16_t stringConstant(std::string const & value) {
    return _code->makeStringConstant(value);
  }

  uint16_t addFunction(BytecodeFunction* function) {
    LOG("adding a function " << function->name() << " to scope " << _id);
    uint16_t id = _code->addFunction(function);
    if (id >= ID_MAX || _functions.find(function->name()) != _functions.end()) return ID_MAX;
    function->setScopeId(_id);
    _functions[function->name()] = id;
    return id;
  }
  
  uint16_t addVariable(std::string const & name, VarType type) {
    LOG("adding a variable " << typeToName(type) << " " << name << " to scope " << _id);
    if (_variables_map.find(name) != _variables_map.end() || _variables.size() >= ID_MAX) return ID_MAX;
    uint16_t newId = _variables.size();
    _variables.push_back(new Var(type, name));
    _variables_map[name] = newId;
    return newId;
  }

  uint16_t id() const {
    return _id;
  }

  VarId resolveVariable(std::string const & varName) const {
    VarMap::const_iterator i = _variables_map.find(varName);
    if (i == _variables_map.end()) {
      return _parent ? _parent->resolveVariable(varName) : std::make_pair(ID_MAX, ID_MAX);
    }
    return std::make_pair(_id, i->second);
  }
  
  Var * getVariable(VarId varId) {
    return getVariable(varId.first, varId.second);
  }

  Var * getVariable(uint16_t scopeId, uint16_t varId) {
    if (scopeId >= _all_scopes->size()) return NULL;
    BytecodeScope * scope = (*_all_scopes)[scopeId];
    if (scope->_variables.size() >= varId) return NULL;
    return scope->_variables[varId];
  }
  
  uint16_t resolveFunction(std::string const & functionName) {
    BytecodeFunction * function = getFunction(functionName);
    return function ? function->id() : ID_MAX;
  }

  BytecodeFunction * getFunction(std::string const & functionName) {
    LOG("looking up a function named " << functionName);
    FunctionMap::iterator i = _functions.find(functionName);
    if (i == _functions.end()) {
      return _parent ? _parent->getFunction(functionName) : NULL;
    }
    return dynamic_cast<BytecodeFunction *>(_code->functionById(i->second));
  }

  BytecodeScope * createChildScope() {
    uint16_t newId = _all_scopes->size();
    LOG("creating a child scope with id " << newId);
    if (newId >= ID_MAX) return NULL;
    BytecodeScope * newScope = new BytecodeScope(this, newId);
    _all_scopes->push_back(newScope);
    return newScope;
  }
  
  ~BytecodeScope() {
    if (!_parent) { 
      for (std::vector<BytecodeScope *>::iterator i = _all_scopes->begin(); i != _all_scopes->end(); ++i) {
        if (*i != this) delete *i;
      }
      delete _all_scopes;
    }
    for (std::vector<Var *>::iterator i = _variables.begin(); i != _variables.end(); ++i) {
      delete *i;
    }
  }
  
  static bool isValidVarId(VarId varId) {
    return !(varId.first == ID_MAX || varId.second == ID_MAX);
  }

private:
  BytecodeScope(BytecodeScope* parent, uint16_t id) : _id(id),  _parent(parent), _code(parent->_code), _all_scopes(parent->_all_scopes) {
  }
  
  BytecodeScope(BytecodeScope &);
  
  BytecodeScope & operator=(BytecodeScope const &);
   
private:
  uint16_t _id;
  BytecodeScope* _parent;
  Code* _code;
  std::vector<BytecodeScope *>* _all_scopes;

private:
  FunctionMap _functions;
  VarMap _variables_map;
  std::vector<Var *> _variables;
};


class ScopedBytecodeGenerator : public AstVisitor {
public:
ScopedBytecodeGenerator(BytecodeScope * scope, BytecodeFunction * function) : _scope(scope), _current_function(function) {
}

void visitBinaryOpNode(BinaryOpNode * binaryOpNode) {
  LOG("processing binary op node at " << binaryOpNode->position());
  binaryOpNode->left()->visit(this);
  VarType leftType = _last_expression_type;
  binaryOpNode->right()->visit(this);
  VarType rightType = _last_expression_type;
  addBinaryOperation(binaryOpNode->kind(), leftType, rightType, binaryOpNode->position());
}

void visitUnaryOpNode(UnaryOpNode * unaryOpNode) {
  LOG("processing unary op node at " << unaryOpNode->position());
  unaryOpNode->visitChildren(this);
  switch (unaryOpNode->kind()) {
    case tNOT: {
      addNegation(unaryOpNode->position());
      break;
    }
    case tSUB:  {
      addChangeSign(unaryOpNode->position());
      break;
    }
    default: {
      abort(std::string("Invalid unary operator: ") + tokenStr(unaryOpNode->kind()), unaryOpNode->position());
      break;
    }
  }
}

void visitStringLiteralNode(StringLiteralNode * stringLiteralNode) {
  uint16_t constantId = _scope->stringConstant(stringLiteralNode->literal());
  addInstruction(BC_SLOAD);
  addId(constantId);
  _last_expression_type = VT_STRING;
}

void visitDoubleLiteralNode(DoubleLiteralNode * doubleLiteralNode) {
  addDouble(doubleLiteralNode->literal());
  _last_expression_type = VT_DOUBLE;
} 

void visitIntLiteralNode(IntLiteralNode * intLiteralNode) {
  addInt(intLiteralNode->literal());
  _last_expression_type = VT_INT;
}

void visitLoadNode(LoadNode * loadNode) {
  LOG("processing load node at " << loadNode->position());
  AstVar const * v = loadNode->var();
  BytecodeScope::VarId resolvedVarId = _scope->resolveVariable(v->name());
  if (_scope->isValidVarId(resolvedVarId)) {
    addLoadVar(resolvedVarId, loadNode->position());
  } else {
    abort(std::string("Unresolved variable: ") + typeToName(v->type()) + " " + v->name(), loadNode->position());
  }
}

void visitStoreNode(StoreNode * storeNode) {
  throw std::logic_error("NOT IMPLEMENTED");
}

void visitForNode(ForNode * forNode) {
  throw std::logic_error("NOT IMPLEMENTED");
}

void visitWhileNode(WhileNode * whileNode) {
  throw std::logic_error("NOT IMPLEMENTED");
}

void visitIfNode(IfNode * ifNode) {
  throw std::logic_error("NOT IMPLEMENTED");
}

void visitBlockNode(BlockNode * blockNode) {
  LOG("processing block node at " << blockNode->position());
  LOG("adding block variables");
  for (Scope::VarIterator i(blockNode->scope()); i.hasNext();) {
    AstVar* var = i.next();
    _scope->addVariable(var->name(), var->type());
  }

  LOG("adding block functions");
  for (Scope::FunctionIterator i(blockNode->scope()); i.hasNext();) {
    _scope->addFunction(new BytecodeFunction(i.next()));
  }
 
  LOG("generating block's code");
  blockNode->visitChildren(this);
  
  LOG("generating block's functions");
  for (Scope::FunctionIterator i(blockNode->scope()); i.hasNext();) {
    visitFunctionNode(i.next()->node());
  }
}

void visitFunctionNode(FunctionNode * functionNode) {
  LOG("generating code for function " << functionNode->name());
  BytecodeScope * childScope = _scope->createChildScope();
  for (uint32_t i = 0;  i != functionNode->parametersNumber(); ++i) {
    uint16_t varId = childScope->addVariable(functionNode->parameterName(i), functionNode->parameterType(i));
    addStoreNonCtxVar(varId, functionNode->parameterType(i), functionNode->position());
  }
  ScopedBytecodeGenerator(childScope, _scope->getFunction(functionNode->name())).visitBlockNode(functionNode->body());
  //TODO validate generated code ?
}

void visitReturnNode(ReturnNode * returnNode) {
  LOG("processing return node at " << returnNode->position());
  if (returnNode->returnExpr()) {
    throw std::logic_error("NOT IMPLEMENTED");
  }
  addInstruction(BC_RETURN);
}

void visitCallNode(CallNode * callNode) {
  LOG("processing call node at " << callNode->position());
  for (uint32_t i = callNode->parametersNumber() - 1; i >= 0; --i) {
    callNode->parameterAt(i)->visit(this);
  }
  uint16_t functionId = _scope->resolveFunction(callNode->name());
  if (functionId < ID_MAX) {
    addInstruction(BC_CALL);
    addId(functionId);
  } else {
    abort(std::string("Unresolved function: ") + callNode->name() + ".", callNode->position());
  }
}

void visitNativeCallNode(NativeCallNode * NativeCallNode) {
  throw std::logic_error("NOT IMPLEMENTED");
}

void visitPrintNode(PrintNode * printNode) {
  throw std::logic_error("NOT IMPLEMENTED");
}

Status getStatus() {
  return _status;
}

private:
void addInstruction(Instruction instruction) {
  bc()->addInsn(instruction);
}

void addId(uint16_t id) {
  bc()->addUInt16(id);
}

void addInt(int64_t value) {
  bc()->addInt64(value);
}

void addDouble(double value) {
  bc()->addDouble(value);
}

void addNegation(uint32_t position) {
  switch (_last_expression_type) {
    case VT_INT: {
      addInstruction(BC_INEG);
      break;
    }
    case VT_DOUBLE: {
      addInstruction(BC_DNEG);
      break;
    }
    default: {
      abort("Cannot negate a value of a non-numeric type.", position);
      break;
    }
  }
}

void addChangeSign(uint32_t position) {
  switch (_last_expression_type) {
    case VT_INT: {
      addInstruction(BC_ILOAD0);
      addInstruction(BC_ISUB);
      break;
    }
    case VT_DOUBLE: {
      addInstruction(BC_DLOAD0);
      addInstruction(BC_DSUB);
      break;
    }
    default: {
      abort("Cannot change sign of  a value of a non-numeric type.", position);
      break;
    }
  }
}

void addLoadVar(BytecodeScope::VarId varId, uint32_t position) {
  uint16_t scopeId = varId.first;
  VarType varType = _scope->getVariable(varId)->type();
  if (isCtxVar(varId)) {
    addLoadCtxVar(scopeId, varId.second, varType, position);
  } else {
    addLoadNonCtxVar(varId.second, varType, position);
  }
}

void addLoadCtxVar(uint16_t scopeId, uint16_t varId, VarType type, uint32_t position) {
  switch (type) {
    case VT_STRING: addInstruction(BC_LOADCTXSVAR); break;
    case VT_INT: addInstruction(BC_LOADCTXIVAR); break;
    case VT_DOUBLE: addInstruction(BC_LOADCTXDVAR); break;
    default: abort(std::string("Cannot load context variable of type: ") + typeToName(type), position); return;
  }
  addId(scopeId);
  addId(varId);
}

void addLoadNonCtxVar(uint16_t varId, VarType type, uint32_t position) {
  switch (type) {
    case VT_STRING: addInstruction(BC_LOADSVAR); break;
    case VT_INT: addInstruction(BC_LOADIVAR); break;
    case VT_DOUBLE: addInstruction(BC_LOADDVAR); break;
    default: abort(std::string("Cannot load variable of type: ") + typeToName(type), position); return;
  }
  addId(varId);
}

void addStoreVar(BytecodeScope::VarId varId, VarType type, uint32_t position) {
  if (isCtxVar(varId)) {
    addStoreCtxVar(varId.first, varId.second, type, position);
  } else {
    addStoreNonCtxVar(varId.second, type, position);
  }
}

void addStoreCtxVar(uint16_t scopeId, uint16_t varId, VarType type, uint32_t position) {
  switch (type) {
    case VT_STRING: addInstruction(BC_STORECTXSVAR); break;
    case VT_INT: addInstruction(BC_STORECTXIVAR); break;
    case VT_DOUBLE: addInstruction(BC_STORECTXDVAR); break;
    default: abort(std::string("Cannot store context variable of type: ") + typeToName(type), position); return;
  }
  addId(scopeId);
  addId(varId);
}

void addStoreNonCtxVar(uint16_t varId, VarType type, uint32_t position) {
  switch (type) {
    case VT_STRING: addInstruction(BC_STORESVAR); break;
    case VT_INT: addInstruction(BC_STOREIVAR); break;
    case VT_DOUBLE: addInstruction(BC_STOREDVAR); break;
    default: abort(std::string("Cannot store variable of type: ") + typeToName(type), position); return;
  }
  addId(varId);
}

bool isCtxVar(BytecodeScope::VarId varId) {
  return varId.first != _scope->id();
}

void addBinaryOperation(TokenKind op, VarType leftType, VarType rightType, uint32_t position) {
  switch (op) {
    //int operands only
    case tOR:
    case tAND:
    case tAOR:
    case tAAND:
    case tAXOR:
    case tMOD: {
      addCastBinaryOperationArgumentsTo(VT_INT, leftType, rightType, position);
      addBinaryOperationInstruction(op, VT_INT, position);
      break;
    }
    //int or double operands
    case tEQ:
    case tNEQ:
    case tGT:
    case tGE:
    case tLT:
    case tLE:
    case tADD:
    case tSUB:
    case tMUL:
    case tDIV: {
      VarType argsType = leastCommonType(leftType, rightType);
      addCastBinaryOperationArgumentsTo(argsType, leftType, rightType, position);
      addBinaryOperationInstruction(op, argsType, position);
      break; 
    }
    default: {
      abort(std::string("Unknown binary operation kind: ") + tokenStr(op), position);
      return;
    }
  }
}

void addCastBinaryOperationArgumentsTo(VarType type, VarType leftType, VarType rightType, uint32_t position) {
  if (leftType != type) {
    addSwap();
    _last_expression_type = leftType;
    addCastTo(type, position);
    _last_expression_type = rightType;
    addSwap();
  }
  if (rightType != type) {
    addCastTo(type, position);
    _last_expression_type = type;
  }
}

void addCastTo(VarType type, uint32_t position) {
  switch (type) {
    case VT_INT: addCastToInt(position); break;
    case VT_DOUBLE: addCastToDouble(position); break;
    default: abort(std::string("No conversions to type ") + typeToName(type) + " available.", position); break;
  }
}

void addCastToInt(uint32_t position) {
  switch (_last_expression_type) {
    case VT_INT: break;
    case VT_STRING: addInstruction(BC_S2I); break;
    case VT_DOUBLE: addInstruction(BC_D2I); break;
    default: {
      abort(std::string("No conversions from type ") + typeToName(_last_expression_type) + " to " + typeToName(VT_INT) + " available.", position); 
      break;
    }
  }
}

void addCastToDouble(uint32_t position) {
  switch (_last_expression_type) {
    case VT_DOUBLE: break;
    case VT_STRING: addInstruction(BC_S2I); addInstruction(BC_I2D); break;
    case VT_INT: addInstruction(BC_I2D); break;
    default: {
      abort(std::string("No conversions from type ") + typeToName(_last_expression_type) + " to " + typeToName(VT_DOUBLE) + " available.", position);
      break;
    }
  }
}

void addSwap() {
  addInstruction(BC_SWAP);
}

void addBinaryOperationInstruction(TokenKind op, VarType operandsType, uint32_t position) {
  switch (op) {
    case tOR: 
    case tAOR: addInstruction(BC_IAOR); _last_expression_type = VT_INT; break;
    case tAND: 
    case tAAND: addInstruction(BC_IAAND); _last_expression_type = VT_INT; break;
    case tAXOR: addInstruction(BC_IAXOR); _last_expression_type = VT_INT; break;
    case tEQ:
    case tNEQ: {
      addInstruction(operandsType == VT_INT ? BC_ICMP : BC_DCMP); 
      _last_expression_type = VT_INT;
      if (op == tEQ) addNegation(position);
      break;
    }
    case tLT:
    case tGT: {
      addInstruction(operandsType == VT_INT ? BC_ICMP : BC_DCMP);
      addInstruction(op == tGT ? BC_ILOAD1 : BC_ILOADM1);
      _last_expression_type = VT_INT;
      addInstruction(BC_ICMP);
      addNegation(position);
      break;
    }
    case tLE:
    case tGE: {
      addInstruction(operandsType == VT_INT ? BC_ICMP : BC_DCMP);
      addInstruction(op == tGE ? BC_ILOADM1 : BC_ILOAD1);
      addInstruction(BC_ICMP);
      _last_expression_type = VT_INT;
      break;
    }
    case tADD: addInstruction(operandsType == VT_INT ? BC_IADD : BC_DADD); _last_expression_type = operandsType; break;
    case tSUB: addInstruction(operandsType == VT_INT ? BC_ISUB : BC_DSUB); _last_expression_type = operandsType; break;
    case tMUL: addInstruction(operandsType == VT_INT ? BC_IMUL : BC_DMUL); _last_expression_type = operandsType; break;
    case tDIV: addInstruction(operandsType == VT_INT ? BC_IDIV : BC_DDIV); _last_expression_type = operandsType; break;
    case tMOD: addInstruction(BC_IMOD); _last_expression_type = VT_INT; break;
    default: {
      // should never be executed.
      throw std::logic_error(std::string("Binary operation ") + tokenStr(op) + " is not supported.");
    }
  }
}

VarType leastCommonType(VarType t1, VarType t2) {
  if (t1 == VT_STRING || t2 == VT_STRING) return VT_DOUBLE;
  if (t1 == VT_DOUBLE || t2 == VT_DOUBLE) return VT_DOUBLE;
  return VT_INT;
}

Bytecode * bc() {
  return _current_function->bytecode();
}

void abort(std::string const & message, uint32_t position) {
  LOG("Abort with message: " << message);
  // do not wipe the very first abort reason
  if (_status.isOk()) {
    _status = Status(message, position);
  }
  //throw ?  
}



private:
  BytecodeScope * _scope;
  BytecodeFunction * _current_function;
  VarType _last_expression_type;
  Status _status;
};

Status* BytecodeTranslatorImpl::translate(std::string const & program, Code** code) {
  Parser parser;
  std::auto_ptr<Status> status(parser.parseProgram(program));
  
  if (status.get() != NULL && status->isError()) {
    std::cerr << "Parser reports error at " << status->getPosition() << ":" << std::endl;
    std::cerr << status->getError() << std::endl;
    return 0;
  }
  
  Code * translatedCode = new InterpreterCodeImpl;
  BytecodeScope rootScope(translatedCode); 
  rootScope.addFunction(new BytecodeFunction(parser.top()));
  ScopedBytecodeGenerator(&rootScope, NULL).visitFunctionNode(parser.top()->node());
  
  LOG("TRANSLATION IS COMPLETE");
  //TODO check if everything's fine before setting code
  *code = translatedCode;
  return new Status;
}

}