#include <iostream>

#include "CompareVisitor.h"

#define ABS(a) ((a) < 0 ? -(a) : (a))
#define EQ(a, b) (ABS((a) - (b)) < 0.00001)

#define TEST(t) \
    if (!(t)) { \
        res = false; \
        return; \
    }

#define DEFINE(t) \
    mathvm::t* a = dynamic_cast<mathvm::t*>(another);

#define INIT(t) DEFINE(t) TEST(a)

#define CHECK(t) TEST(a->t() == node->t())

#define REC(t) { \
    another = a->t; \
    node->t->visit(this); \
}

CompareVisitor::CompareVisitor(mathvm::AstNode* o): res(true), another(o) {}

bool CompareVisitor::compare(mathvm::AstNode* node, mathvm::AstNode* o) {
    if (o != 0) {
        another = o;
    }
    node->visit(this);
    return res;
}

void CompareVisitor::visitBinaryOpNode(mathvm::BinaryOpNode* node) {
    INIT(BinaryOpNode)
    CHECK(kind)
    REC(left())
    REC(right())
}

void CompareVisitor::visitUnaryOpNode(mathvm::UnaryOpNode* node) {
    INIT(UnaryOpNode)
    CHECK(kind)
    REC(operand())
}

void CompareVisitor::visitStringLiteralNode(mathvm::StringLiteralNode* node) {
    INIT(StringLiteralNode)
    CHECK(literal)
}

void CompareVisitor::visitDoubleLiteralNode(mathvm::DoubleLiteralNode* node) {
    DEFINE(DoubleLiteralNode)
    if (a) {
        TEST(EQ(a->literal(), node->literal()))
        return;
    }
    {   INIT(IntLiteralNode)
        TEST(EQ(a->literal(), node->literal()))
    }
}

void CompareVisitor::visitIntLiteralNode(mathvm::IntLiteralNode* node) {
    DEFINE(IntLiteralNode)
    if (a) {
        CHECK(literal)
        return;
    }
    {   INIT(DoubleLiteralNode)
        TEST(EQ(a->literal(), node->literal()))
    }
}

void CompareVisitor::visitLoadNode(mathvm::LoadNode* node) {
    INIT(LoadNode)
    CHECK(var()->type);
    CHECK(var()->name);
}

void CompareVisitor::visitStoreNode(mathvm::StoreNode* node) {
    INIT(StoreNode)
    CHECK(var()->type)
    CHECK(var()->name)
    CHECK(op)
    REC(value())
}

void CompareVisitor::visitForNode(mathvm::ForNode* node) {
    INIT(ForNode)
    CHECK(var()->type)
    CHECK(var()->name)
    REC(inExpr())
    REC(body())
}

void CompareVisitor::visitWhileNode(mathvm::WhileNode* node) {
    INIT(WhileNode)
    REC(whileExpr())
    REC(loopBlock())
}

void CompareVisitor::visitIfNode(mathvm::IfNode* node) {
    INIT(IfNode)
    REC(ifExpr())
    REC(thenBlock())
    TEST((node->elseBlock() && a->elseBlock()) || (!node->elseBlock() && !a->elseBlock()))
    if (node->elseBlock()) REC(elseBlock())
}

void CompareVisitor::visitBlockNode(mathvm::BlockNode* node) {
    INIT(BlockNode)
    mathvm::Scope::VarIterator it(node->scope());
    mathvm::Scope::VarIterator it1(a->scope());
    while (it.hasNext()) {
        TEST(it1.hasNext())
        mathvm::AstVar *v = it.next(), *v1 = it1.next();
        TEST(v->type() == v1->type())
        TEST(v->name() == v1->name())
    }
    TEST(!it1.hasNext())
    for (uint32_t i = 0; i < node->nodes(); ++i) {
        REC(nodeAt(i))
    }
}

void CompareVisitor::visitFunctionNode(mathvm::FunctionNode* node) {
    INIT(FunctionNode)
    CHECK(name)
    REC(args())
    REC(body())
}

void CompareVisitor::visitPrintNode(mathvm::PrintNode* node) {
    INIT(PrintNode)
    for (uint32_t i = 0; i < node->operands(); ++i) {
        REC(operandAt(i))
    }
}
