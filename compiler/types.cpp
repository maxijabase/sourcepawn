/* vim: set sts=2 ts=8 sw=2 tw=99 et: */
/*  Pawn compiler
 *
 *  Function and variable definition and declaration, statement parser.
 *
 *  Copyright (c) ITB CompuPhase, 1997-2006
 *
 *  This software is provided "as-is", without any express or implied warranty.
 *  In no event will the authors be held liable for any damages arising from
 *  the use of this software.
 *
 *  Permission is granted to anyone to use this software for any purpose,
 *  including commercial applications, and to alter it and redistribute it
 *  freely, subject to the following restrictions:
 *
 *  1.  The origin of this software must not be misrepresented; you must not
 *      claim that you wrote the original software. If you use this software in
 *      a product, an acknowledgment in the product documentation would be
 *      appreciated but is not required.
 *  2.  Altered source versions must be plainly marked as such, and must not be
 *      misrepresented as being the original software.
 *  3.  This notice may not be removed or altered from any source distribution.
 *
 *  Version: $Id$
 */
#include <ctype.h>

#include <utility>

#include "types.h"
#include "sc.h"
#include "sctracker.h"
#include "scvars.h"

using namespace ke;

TypeDictionary gTypes;

Type::Type(sp::Atom* name, cell value)
 : name_(name),
   value_(value),
   fixed_(0),
   intrinsic_(false),
   first_pass_kind_(TypeKind::None),
   kind_(TypeKind::None)
{
    private_ptr_ = nullptr;
}

void
Type::resetPtr()
{
    // We try to persist tag information across passes, since globals are
    // preserved and core types should be too. However user-defined types
    // that attach extra structural information are cleared, as that
    // data is not retained into the statWRITE pass.
    if (intrinsic_)
        return;

    if (kind_ != TypeKind::None)
        first_pass_kind_ = kind_;
    kind_ = TypeKind::None;
    private_ptr_ = nullptr;
}

bool
Type::isDeclaredButNotDefined() const
{
    if (kind_ != TypeKind::None)
        return false;
    if (first_pass_kind_ == TypeKind::None || first_pass_kind_ == TypeKind::EnumStruct) {
        return true;
    }
    return false;
}

const char*
Type::prettyName() const
{
  if (kind_ == TypeKind::Function)
    return kindName();
  if (tagid() == 0)
    return "int";
  return name();
}

const char*
Type::kindName() const
{
  switch (kind_) {
    case TypeKind::EnumStruct:
      return "enum struct";
    case TypeKind::Struct:
      return "struct";
    case TypeKind::Methodmap:
      return "methodmap";
    case TypeKind::Enum:
      return "enum";
    case TypeKind::Object:
      return "object";
    case TypeKind::Function:
      if (funcenum_ptr_) {
        if (funcenum_ptr_->entries.size() > 1)
          return "typeset";
        if (ke::StartsWith(name_->chars(), "::"))
          return "function";
        return "typedef";
      }
      return "function";
    default:
      return "type";
  }
}

bool
Type::isLabelTag() const
{
    if (tagid() == 0 || tagid() == pc_tag_bool || tagid() == sc_rationaltag)
        return false;
    return kind_ == TypeKind::None;
}

TypeDictionary::TypeDictionary() {}

Type*
TypeDictionary::find(sp::Atom* name)
{
    for (const auto& type : types_) {
        if (type->nameAtom() == name)
            return type.get();
    }
    return nullptr;
}

Type*
TypeDictionary::find(int tag)
{
    assert(size_t(tag) < types_.size());

    return types_[tag].get();
}

Type*
TypeDictionary::findOrAdd(const char* name)
{
    sp::Atom* atom = gAtoms.add(name);
    for (const auto& type : types_) {
        if (type->nameAtom() == atom)
            return type.get();
    }

    int tag = int(types_.size());
    std::unique_ptr<Type> type = std::make_unique<Type>(atom, tag);
    types_.push_back(std::move(type));
    return types_.back().get();
}

void
TypeDictionary::clear()
{
    types_.clear();
}

void
TypeDictionary::clearExtendedTypes()
{
    for (const auto& type : types_)
        type->resetPtr();
}

void
TypeDictionary::init()
{
    Type* type = findOrAdd("_");
    assert(type->tagid() == 0);

    type = defineBool();
    assert(type->tagid() == 1);

    pc_tag_bool = type->tagid();
    tag_any_ = defineAny()->tagid();
    tag_function_ = defineFunction("Function", nullptr)->tagid();
    pc_tag_string = defineString()->tagid();
    sc_rationaltag = defineFloat()->tagid();
    tag_void_ = defineVoid()->tagid();
    tag_object_ = defineObject("object")->tagid();
    tag_null_ = defineObject("null_t")->tagid();
    tag_nullfunc_ = defineObject("nullfunc_t")->tagid();

    for (const auto& type : types_)
        type->setIntrinsic();
}

Type*
TypeDictionary::defineAny()
{
    return findOrAdd("any");
}

Type*
TypeDictionary::defineFunction(const char* name, funcenum_t* fe)
{
    Type* type = findOrAdd(name);
    type->setFunction(fe);
    return type;
}

Type*
TypeDictionary::defineString()
{
    Type* type = findOrAdd("String");
    type->setFixed();
    return type;
}

Type*
TypeDictionary::defineFloat()
{
    Type* type = findOrAdd("Float");
    type->setFixed();
    return type;
}

Type*
TypeDictionary::defineVoid()
{
    Type* type = findOrAdd("void");
    type->setFixed();
    return type;
}

Type*
TypeDictionary::defineObject(const char* name)
{
    Type* type = findOrAdd(name);
    type->setObject();
    return type;
}

Type*
TypeDictionary::defineBool()
{
    return findOrAdd("bool");
}

Type*
TypeDictionary::defineMethodmap(const char* name, methodmap_t* map)
{
    Type* type = findOrAdd(name);
    type->setMethodmap(map);
    return type;
}

Type*
TypeDictionary::defineEnumTag(const char* name)
{
    Type* type = findOrAdd(name);
    type->setEnumTag();
    if (isupper(*name))
        type->setFixed();
    return type;
}

Type*
TypeDictionary::defineEnumStruct(const char* name, symbol* sym)
{
    Type* type = findOrAdd(name);
    type->setEnumStruct(sym);
    return type;
}

Type*
TypeDictionary::defineTag(const char* name)
{
    Type* type = findOrAdd(name);
    if (isupper(*name))
        type->setFixed();
    return type;
}

Type*
TypeDictionary::definePStruct(const char* name, pstruct_t* ps)
{
    Type* type = findOrAdd(name);
    type->setStruct(ps);
    return type;
}

const char*
pc_tagname(int tag)
{
    if (Type* type = gTypes.find(tag))
        return type->name();
    return "__unknown__";
}

bool
typeinfo_t::isCharArray() const
{
    return numdim() == 1 && tag() == pc_tag_string;
}
