// vim: set ts=8 sts=4 sw=4 tw=99 et:
//
//  Copyright (c) 2021 AlliedModders LLC
//
//  This software is provided "as-is", without any express or implied warranty.
//  In no event will the authors be held liable for any damages arising from
//  the use of this software.
//
//  Permission is granted to anyone to use this software for any purpose,
//  including commercial applications, and to alter it and redistribute it
//  freely, subject to the following restrictions:
//
//  1.  The origin of this software must not be misrepresented; you must not
//      claim that you wrote the original software. If you use this software in
//      a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//  2.  Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//  3.  This notice may not be removed or altered from any source distribution.
#include "parse-node.h"

#include "errors.h"

VarDecl::VarDecl(const token_pos_t& pos, sp::Atom* name, const typeinfo_t& type, int vclass,
                 bool is_public, bool is_static, bool is_stock, Expr* initializer)
 : Decl(AstKind::VarDecl, pos, name),
   type_(type),
   vclass_(vclass),
   is_public_(is_public),
   is_static_(is_static),
   is_stock_(is_stock),
   autozero_(true)
{
    // Having a BinaryExpr allows us to re-use assignment logic.
    if (initializer)
        set_init(initializer);
}

void
VarDecl::set_init(Expr* expr)
{
    init_ = new BinaryExpr(pos(), '=', new SymbolExpr(pos(), name()), expr);
    init_->set_initializer();
}

Expr*
VarDecl::init_rhs() const
{
    if (!init_)
        return nullptr;
    return init_->right();
}

void
ParseNode::error(const token_pos_t& pos, int number, ...)
{
    va_list ap;
    va_start(ap, number);
    error_va(pos, number, ap);
    va_end(ap);
}

void
Expr::FlattenLogical(int token, std::vector<Expr*>* out)
{
    out->push_back(this);
}

void
LogicalExpr::FlattenLogical(int token, std::vector<Expr*>* out)
{
    if (token_ == token) {
        left_->FlattenLogical(token, out);
        right_->FlattenLogical(token, out);
    } else {
        Expr::FlattenLogical(token, out);
    }
}

BlockStmt*
BlockStmt::WrapStmt(Stmt* stmt)
{
    if (BlockStmt* block = stmt->as<BlockStmt>())
        return block;
    BlockStmt* block = new BlockStmt(stmt->pos());
    block->stmts().emplace_back(stmt);
    return block;
}

BinaryExprBase::BinaryExprBase(AstKind kind, const token_pos_t& pos, int token, Expr* left, Expr* right)
  : Expr(kind, pos),
    token_(token),
    left_(left),
    right_(right)
{
    assert(right_ != this);
}
