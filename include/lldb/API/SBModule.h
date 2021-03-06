//===-- SBModule.h ----------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBModule_h_
#define LLDB_SBModule_h_

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBSection.h"
#include "lldb/API/SBSymbolContext.h"
#include "lldb/API/SBValueList.h"

namespace lldb {

class SBModule
{
public:

    SBModule ();

    SBModule (const SBModule &rhs);
    
#ifndef SWIG
    const SBModule &
    operator = (const SBModule &rhs);
#endif

    ~SBModule ();

    bool
    IsValid () const;

    void
    Clear();

    //------------------------------------------------------------------
    /// Get const accessor for the module file specification.
    ///
    /// This function returns the file for the module on the host system
    /// that is running LLDB. This can differ from the path on the 
    /// platform since we might be doing remote debugging.
    ///
    /// @return
    ///     A const reference to the file specification object.
    //------------------------------------------------------------------
    lldb::SBFileSpec
    GetFileSpec () const;

    //------------------------------------------------------------------
    /// Get accessor for the module platform file specification.
    ///
    /// Platform file refers to the path of the module as it is known on
    /// the remote system on which it is being debugged. For local 
    /// debugging this is always the same as Module::GetFileSpec(). But
    /// remote debugging might mention a file '/usr/lib/liba.dylib'
    /// which might be locally downloaded and cached. In this case the
    /// platform file could be something like:
    /// '/tmp/lldb/platform-cache/remote.host.computer/usr/lib/liba.dylib'
    /// The file could also be cached in a local developer kit directory.
    ///
    /// @return
    ///     A const reference to the file specification object.
    //------------------------------------------------------------------
    lldb::SBFileSpec
    GetPlatformFileSpec () const;

    bool
    SetPlatformFileSpec (const lldb::SBFileSpec &platform_file);

#ifndef SWIG
    const uint8_t *
    GetUUIDBytes () const;
#endif

    const char *
    GetUUIDString () const;

#ifndef SWIG
    bool
    operator == (const lldb::SBModule &rhs) const;

    bool
    operator != (const lldb::SBModule &rhs) const;

#endif
    lldb::SBSection
    FindSection (const char *sect_name);

    lldb::SBAddress
    ResolveFileAddress (lldb::addr_t vm_addr);

    lldb::SBSymbolContext
    ResolveSymbolContextForAddress (const lldb::SBAddress& addr, 
                                    uint32_t resolve_scope);

    bool
    GetDescription (lldb::SBStream &description);

    size_t
    GetNumSymbols ();
    
    lldb::SBSymbol
    GetSymbolAtIndex (size_t idx);

    size_t
    GetNumSections ();
    
    lldb::SBSection
    GetSectionAtIndex (size_t idx);
    //------------------------------------------------------------------
    /// Find functions by name.
    ///
    /// @param[in] name
    ///     The name of the function we are looking for.
    ///
    /// @param[in] name_type_mask
    ///     A logical OR of one or more FunctionNameType enum bits that
    ///     indicate what kind of names should be used when doing the
    ///     lookup. Bits include fully qualified names, base names,
    ///     C++ methods, or ObjC selectors. 
    ///     See FunctionNameType for more details.
    ///
    /// @param[in] append
    ///     If true, any matches will be appended to \a sc_list, else
    ///     matches replace the contents of \a sc_list.
    ///
    /// @param[out] sc_list
    ///     A symbol context list that gets filled in with all of the
    ///     matches.
    ///
    /// @return
    ///     The number of matches added to \a sc_list.
    //------------------------------------------------------------------
    uint32_t
    FindFunctions (const char *name, 
                   uint32_t name_type_mask, // Logical OR one or more FunctionNameType enum bits
                   bool append, 
                   lldb::SBSymbolContextList& sc_list);

    //------------------------------------------------------------------
    /// Find global and static variables by name.
    ///
    /// @param[in] target
    ///     A valid SBTarget instance representing the debuggee.
    ///
    /// @param[in] name
    ///     The name of the global or static variable we are looking
    ///     for.
    ///
    /// @param[in] max_matches
    ///     Allow the number of matches to be limited to \a max_matches.
    ///
    /// @return
    ///     A list of matched variables in an SBValueList.
    //------------------------------------------------------------------
    lldb::SBValueList
    FindGlobalVariables (lldb::SBTarget &target, 
                         const char *name, 
                         uint32_t max_matches);
    
    lldb::SBType
    FindFirstType (const char* name);
    
    lldb::SBTypeList
    FindTypes (const char* type);
    
private:
    friend class SBAddress;
    friend class SBFrame;
    friend class SBSection;
    friend class SBSymbolContext;
    friend class SBTarget;

    explicit SBModule (const lldb::ModuleSP& module_sp);

    void
    SetModule (const lldb::ModuleSP& module_sp);
#ifndef SWIG

    lldb::ModuleSP &
    operator *();


    lldb_private::Module *
    operator ->();

    const lldb_private::Module *
    operator ->() const;

    lldb_private::Module *
    get();

    const lldb_private::Module *
    get() const;

    const lldb::ModuleSP &
    get_sp() const;
    

#endif

    lldb::ModuleSP m_opaque_sp;
};


} // namespace lldb

#endif // LLDB_SBModule_h_
