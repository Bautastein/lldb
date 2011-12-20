//===-- SWIG Interface for SBSymbolContextList ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

namespace lldb {

%feature("docstring",
"Represents a list of symbol context object. See also SBSymbolContext.

For example (from test/python_api/target/TestTargetAPI.py),

    def find_functions(self, exe_name):
        '''Exercise SBTaget.FindFunctions() API.'''
        exe = os.path.join(os.getcwd(), exe_name)

        # Create a target by the debugger.
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)

        list = lldb.SBSymbolContextList()
        num = target.FindFunctions('c', lldb.eFunctionNameTypeAuto, False, list)
        self.assertTrue(num == 1 and list.GetSize() == 1)

        for sc in list:
            self.assertTrue(sc.GetModule().GetFileSpec().GetFilename() == exe_name)
            self.assertTrue(sc.GetSymbol().GetName() == 'c')                
") SBSymbolContextList;
class SBSymbolContextList
{
public:
    SBSymbolContextList ();

    SBSymbolContextList (const lldb::SBSymbolContextList& rhs);

    ~SBSymbolContextList ();

    bool
    IsValid () const;

    uint32_t
    GetSize() const;

    SBSymbolContext
    GetContextAtIndex (uint32_t idx);

    void
    Clear();
};

} // namespace lldb
