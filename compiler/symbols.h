// vim: set ts=8 sts=4 sw=4 tw=99 et:
//  Pawn compiler - Recursive descend expresion parser
//
//  Copyright (c) ITB CompuPhase, 1997-2005
//  Copyright (c) AlliedModders LLC 2021
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
//

#pragma once

#include <functional>
#include <unordered_map>

#include "label.h"
#include "sc.h"

class CompileContext;
class FunctionInfo;
class SemaContext;
struct token_pos_t;

class FunctionData final : public SymbolData
{
  public:
    FunctionData();
    FunctionData* asFunction() override {
        return this;
    }

    void resizeArgs(size_t nargs);

    PoolList<PoolString> dbgstrs;
    PoolList<arginfo> args;
    ArrayData* array;    // For functions with array returns
    FunctionInfo* node;
    FunctionInfo* forward;
    symbol* alias;
    sp::Label label;     // modern replacement for addr
    sp::Label funcid;
};

class EnumStructVarData final : public SymbolData
{
  public:
    EnumStructVarData* asEnumStructVar() override { return this; }

    PoolList<symbol*> children;
};

class EnumData final : public SymbolData
{
  public:
    EnumData* asEnum() override { return this; }

    PoolList<symbol*> children;
};

class EnumStructData final : public SymbolData
{
  public:
    EnumStructData* asEnumStruct() override { return this; }

    PoolList<symbol*> fields;
    PoolList<symbol*> methods;
};

/*  Symbol table format
 *
 *  The symbol name read from the input file is stored in "name", the
 *  value of "addr" is written to the output file. The address in "addr"
 *  depends on the class of the symbol:
 *      global          offset into the data segment
 *      local           offset relative to the stack frame
 *      label           generated hexadecimal number
 *      function        offset into code segment
 */
struct symbol : public PoolObject
{
    symbol(const symbol& other);
    symbol(sp::Atom* name, cell addr, int ident, int vclass, int tag);

    symbol* next;
    cell codeaddr; /* address (in the code segment) where the symbol declaration starts */
    char vclass;   /* sLOCAL if "addr" refers to a local symbol */
    char ident;    /* see below for possible values */
    int tag;       /* tagname id */

    // See uREAD/uWRITTEN above.
    uint8_t usage : 3;

    // Variable: the variable is defined in the source file.
    // Function: the function is defined ("implemented") in the source file
    // Constant: the symbol is defined in the source file.
    bool defined : 1;       // remove when moving to a single-pass system
    bool is_const : 1;

    // Variables and functions.
    bool stock : 1;         // discardable without warning
    bool is_public : 1;     // publicly exposed
    bool is_static : 1;     // declared as static

    // TODO: make this an ident.
    bool is_struct : 1;

    // Functions only.
    bool missing : 1;       // the function is not implemented in this source file
    bool callback : 1;      // used as a callback
    bool native : 1;        // the function is native
    bool returns_value : 1; // whether any path returns a value
    bool always_returns: 1; // whether all paths have an explicit return statement
    bool retvalue_used : 1; // the return value is used
    bool is_operator : 1;

    // Constants only.
    bool enumroot : 1;      // the constant is the "root" of an enumeration
    bool enumfield : 1;     // the constant is a field in a named enumeration

    // General symbol flags.
    bool deprecated : 1;    // symbol is deprecated (avoid use)
    bool queued : 1;        // symbol is queued for a local work algorithm
    bool explicit_return_type : 1; // transitional return type was specified

    union {
        struct {
            int index; /* array & enum: tag of array indices or the enum item */
            int field; /* enumeration fields, where a size is attached to the field */
        } tags;        /* extra tags */
    } x;               /* 'x' for 'extra' */
    union {
        struct {
            cell length;  /* arrays: length (size) */
            short level;  /* number of dimensions below this level */
        } array;
    } dim;       /* for 'dimension', both functions and arrays */
    int fnumber; /* file number in which the symbol is declared */
    int lnumber; /* line number for the declaration */
    PoolString* documentation; /* optional documentation string */

    int addr() const {
        return addr_;
    }
    void setAddr(int addr) {
        addr_ = addr;
    }
    sp::Atom* nameAtom() const {
        return name_;
    }
    const char* name() const {
        return name_ ? name_->chars() : "";
    }
    void setName(sp::Atom* name) {
        name_ = name;
    }
    FunctionData* function() const {
        assert(ident == iFUNCTN);
        return data_->asFunction();
    }
    symbol* parent() const {
        return parent_;
    }
    void set_parent(symbol* parent) {
        parent_ = parent;
    }

    symbol* array_return() const {
        assert(ident == iFUNCTN);
        return child_;
    }
    void set_array_return(symbol* child) {
        assert(ident == iFUNCTN);
        assert(!child_);
        child_ = child;
    }
    symbol* array_child() const {
        assert(ident == iARRAY || ident == iREFARRAY);
        return child_;
    }
    void set_array_child(symbol* child) {
        assert(ident == iARRAY || ident == iREFARRAY);
        assert(!child_);
        child_ = child;
    }
    SymbolData* data() const {
        return data_;
    }
    void set_data(SymbolData* data) {
        data_ = std::move(data);
    }

    void add_reference_to(symbol* other);
    void drop_reference_from(symbol* from);

    PoolList<symbol*>& refers_to() {
        return refers_to_;
    }
    bool is_unreferenced() const {
        return referred_from_count_ == 0;
    }
    void clear_refers() {
        refers_to_.clear();
        referred_from_.clear();
    }
    bool is_variadic() const;
    bool must_return_value() const;
    bool used() const {
        assert(ident == iFUNCTN);
        return (usage & uLIVE) == uLIVE;
    }
    bool unused() const {
        return !used();
    }

  private:
    cell addr_; /* address or offset (or value for constant, index for native function) */
    sp::Atom* name_;
    SymbolData* data_;

    // Other symbols that this symbol refers to.
    PoolList<symbol*> refers_to_;

    // All the symbols that refer to this symbol.
    PoolList<symbol*> referred_from_;
    size_t referred_from_count_;

    symbol* parent_;
    symbol* child_;
};

enum ScopeKind {
    sGLOBAL = 0,      /* global variable/constant class (no states) */
    sLOCAL = 1,       /* local variable/constant */
    sSTATIC = 2,      /* global lifetime, local or global scope */
    sARGUMENT = 3,    /* function argument (this is never stored anywhere) */
    sENUMFIELD = 4,   /* for analysis purposes only (not stored anywhere) */
    sFILE_STATIC = 5, /* only appears on SymbolScope, to clarify sSTATIC */
};

class SymbolScope final : public PoolObject
{
  public:
    SymbolScope(SymbolScope* parent, ScopeKind kind, int fnumber = -1)
      : parent_(parent),
        kind_(kind),
        fnumber_(fnumber)
    {}

    symbol* Find(sp::Atom* atom) const {
        auto iter = symbols_.find(atom);
        if (iter == symbols_.end())
            return nullptr;
        return iter->second;
    }

    void Add(symbol* sym);

    // Add, but allow duplicates by linking together.
    void AddChain(symbol* sym);

    void ForEachSymbol(const std::function<void(symbol*)>& callback) {
        for (const auto& pair : symbols_) {
            for (symbol* iter = pair.second; iter; iter = iter->next)
                callback(iter);
        }
    }

    bool IsGlobalOrFileStatic() const {
        return kind_ == sGLOBAL || kind_ == sFILE_STATIC;
    }
    bool IsLocalOrArgument() const {
        return kind_ == sLOCAL || kind_ == sARGUMENT;
    }

    SymbolScope* parent() const { return parent_; }
    ScopeKind kind() const { return kind_; }
    int fnumber() const { return fnumber_; }

  private:
    SymbolScope* parent_;
    ScopeKind kind_;
    PoolMap<sp::Atom*, symbol*> symbols_;
    int fnumber_;
};

struct value {
    char ident;      /* iCONSTEXPR, iVARIABLE, iARRAY, iARRAYCELL,
                         * iEXPRESSION or iREFERENCE */
    /* symbol in symbol table, NULL for (constant) expression */
    symbol* sym;
    cell constval;   /* value of the constant expression (if ident==iCONSTEXPR)
                         * also used for the size of a literal array */
    int tag;         /* tag (of the expression) */

    // Returns whether the value can be rematerialized based on static
    // information, or whether it is the result of an expression.
    bool canRematerialize() const {
        switch (ident) {
            case iVARIABLE:
            case iCONSTEXPR:
                return true;
            case iREFERENCE:
                return sym->vclass == sARGUMENT || sym->vclass == sLOCAL;
            default:
                return false;
        }
    }

    /* when ident == iACCESSOR */
    methodmap_method_t* accessor;

    static value ErrorValue() {
        value v = {};
        v.ident = iCONSTEXPR;
        return v;
    }
};

/* Wrapper around value + l/rvalue bit. */
struct svalue {
    value val;
    int lvalue;

    bool canRematerialize() const {
        return val.canRematerialize();
    }
};

void AddGlobal(CompileContext& cc, symbol* sym);

symbol* FindSymbol(SymbolScope* scope, sp::Atom* name, SymbolScope** found = nullptr);
symbol* FindSymbol(SemaContext& sc, sp::Atom* name, SymbolScope** found = nullptr);
void DefineSymbol(SemaContext& sc, symbol* sym);
symbol* DefineConstant(CompileContext& cc, sp::Atom* name, cell val, int tag);
symbol* DefineConstant(SemaContext& sc, sp::Atom* name, const token_pos_t& pos, cell val,
                       int vclass, int tag);
bool CheckNameRedefinition(SemaContext& sc, sp::Atom* name, const token_pos_t& pos, int vclass);

void markusage(symbol* sym, int usage);
symbol* NewVariable(sp::Atom* name, cell addr, int ident, int vclass, int tag, int dim[],
                    int numdim, int semantic_tag);
int findnamedarg(arginfo* arg, sp::Atom* name);
symbol* FindEnumStructField(Type* type, sp::Atom* name);
void deduce_liveness(CompileContext& cc);
void declare_handle_intrinsics();
symbol* declare_methodmap_symbol(CompileContext& cc, methodmap_t* map);
