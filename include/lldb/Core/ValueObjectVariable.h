//===-- ValueObjectVariable.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ValueObjectVariable_h_
#define liblldb_ValueObjectVariable_h_

// C Includes
// C++ Includes
// Other libraries and framework includes
// Project includes
#include "lldb/Core/ValueObject.h"

namespace lldb_private {

//----------------------------------------------------------------------
// A ValueObject that contains a root variable that may or may not
// have children.
//----------------------------------------------------------------------
class ValueObjectVariable : public ValueObject
{
public:
    static lldb::ValueObjectSP
    Create (ExecutionContextScope *exe_scope, const lldb::VariableSP &var_sp);

    virtual
    ~ValueObjectVariable();

    virtual size_t
    GetByteSize();

    virtual clang::ASTContext *
    GetClangAST ();

    virtual lldb::clang_type_t
    GetClangType ();

    virtual ConstString
    GetTypeName();

    virtual uint32_t
    CalculateNumChildren();

    virtual lldb::ValueType
    GetValueType() const;

    virtual bool
    IsInScope ();

    virtual Module*
    GetModule();
    
    virtual SymbolContextScope *
    GetSymbolContextScope();

protected:
    virtual bool
    UpdateValue ();

    lldb::VariableSP  m_variable_sp;  ///< The variable that this value object is based upon

private:
    ValueObjectVariable (ExecutionContextScope *exe_scope, const lldb::VariableSP &var_sp);
    //------------------------------------------------------------------
    // For ValueObject only
    //------------------------------------------------------------------
    DISALLOW_COPY_AND_ASSIGN (ValueObjectVariable);
};

} // namespace lldb_private

#endif  // liblldb_ValueObjectVariable_h_
