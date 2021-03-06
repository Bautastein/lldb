//===-- SBCommandInterpreter.cpp --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-types.h"
#include "lldb/Interpreter/Args.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Core/Listener.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/Target.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBTarget.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"

using namespace lldb;
using namespace lldb_private;


SBCommandInterpreter::SBCommandInterpreter (CommandInterpreter *interpreter) :
    m_opaque_ptr (interpreter)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter::SBCommandInterpreter (interpreter=%p)"
                     " => SBCommandInterpreter(%p)", interpreter, m_opaque_ptr);
}

SBCommandInterpreter::SBCommandInterpreter(const SBCommandInterpreter &rhs) :
    m_opaque_ptr (rhs.m_opaque_ptr)
{
}

const SBCommandInterpreter &
SBCommandInterpreter::operator = (const SBCommandInterpreter &rhs)
{
    m_opaque_ptr = rhs.m_opaque_ptr;
    return *this;
}

SBCommandInterpreter::~SBCommandInterpreter ()
{
}

bool
SBCommandInterpreter::IsValid() const
{
    return m_opaque_ptr != NULL;
}


bool
SBCommandInterpreter::CommandExists (const char *cmd)
{
    if (cmd && m_opaque_ptr)
        return m_opaque_ptr->CommandExists (cmd);
    return false;
}

bool
SBCommandInterpreter::AliasExists (const char *cmd)
{
    if (cmd && m_opaque_ptr)
        return m_opaque_ptr->AliasExists (cmd);
    return false;
}

lldb::ReturnStatus
SBCommandInterpreter::HandleCommand (const char *command_line, SBCommandReturnObject &result, bool add_to_history)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::HandleCommand (command=\"%s\", SBCommandReturnObject(%p), add_to_history=%i)", 
                     m_opaque_ptr, command_line, result.get(), add_to_history);

    result.Clear();
    if (command_line && m_opaque_ptr)
    {
        TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
        Mutex::Locker api_locker;
        if (target_sp)
            api_locker.Reset(target_sp->GetAPIMutex().GetMutex());
        m_opaque_ptr->HandleCommand (command_line, add_to_history, result.ref());
    }
    else
    {
        result->AppendError ("SBCommandInterpreter or the command line is not valid");
        result->SetStatus (eReturnStatusFailed);
    }

    // We need to get the value again, in case the command disabled the log!
    log = lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API);
    if (log)
    {
        SBStream sstr;
        result.GetDescription (sstr);
        log->Printf ("SBCommandInterpreter(%p)::HandleCommand (command=\"%s\", SBCommandReturnObject(%p): %s, add_to_history=%i) => %i", 
                     m_opaque_ptr, command_line, result.get(), sstr.GetData(), add_to_history, result.GetStatus());
    }

    return result.GetStatus();
}

int
SBCommandInterpreter::HandleCompletion (const char *current_line,
                                        const char *cursor,
                                        const char *last_char,
                                        int match_start_point,
                                        int max_return_elements,
                                        SBStringList &matches)
{
    int num_completions = 0;
    
    // Sanity check the arguments that are passed in:
    // cursor & last_char have to be within the current_line.
    if (current_line == NULL || cursor == NULL || last_char == NULL)
        return 0;
    
    if (cursor < current_line || last_char < current_line)
        return 0;
        
    size_t current_line_size = strlen (current_line);
    if (cursor - current_line > current_line_size || last_char - current_line > current_line_size)
        return 0;
        
    if (m_opaque_ptr)
    {
        lldb_private::StringList lldb_matches;
        num_completions =  m_opaque_ptr->HandleCompletion (current_line, cursor, last_char, match_start_point,
                                                           max_return_elements, lldb_matches);

        SBStringList temp_list (&lldb_matches);
        matches.AppendList (temp_list);
    }
    return num_completions;
}

int
SBCommandInterpreter::HandleCompletion (const char *current_line,
                  uint32_t cursor_pos,
                  int match_start_point,
                  int max_return_elements,
                  lldb::SBStringList &matches)
{
    const char *cursor = current_line + cursor_pos;
    const char *last_char = current_line + strlen (current_line);
    return HandleCompletion (current_line, cursor, last_char, match_start_point, max_return_elements, matches);
}

bool
SBCommandInterpreter::HasCommands ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->HasCommands();
    return false;
}

bool
SBCommandInterpreter::HasAliases ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->HasAliases();
    return false;
}

bool
SBCommandInterpreter::HasAliasOptions ()
{
    if (m_opaque_ptr)
        return m_opaque_ptr->HasAliasOptions ();
    return false;
}

SBProcess
SBCommandInterpreter::GetProcess ()
{
    SBProcess process;
    if (m_opaque_ptr)
    {
        TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
        if (target_sp)
        {
            Mutex::Locker api_locker(target_sp->GetAPIMutex());
            process.SetProcess(target_sp->GetProcessSP());
        }
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::GetProcess () => SBProcess(%p)", 
                     m_opaque_ptr, process.get());

    
    return process;
}

ssize_t
SBCommandInterpreter::WriteToScriptInterpreter (const char *src)
{
    return WriteToScriptInterpreter (src, strlen(src));
}

ssize_t
SBCommandInterpreter::WriteToScriptInterpreter (const char *src, size_t src_len)
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    ssize_t bytes_written = 0;
    if (m_opaque_ptr && src && src[0])
    {
        ScriptInterpreter *script_interpreter = m_opaque_ptr->GetScriptInterpreter();
        if (script_interpreter)
            bytes_written = ::write (script_interpreter->GetMasterFileDescriptor(), src, src_len);
    }
    if (log)
        log->Printf ("SBCommandInterpreter(%p)::WriteToScriptInterpreter (src=\"%s\", src_len=%zu) => %zi", 
                     m_opaque_ptr, src, src_len, bytes_written);

    return bytes_written;
}


CommandInterpreter *
SBCommandInterpreter::get ()
{
    return m_opaque_ptr;
}

CommandInterpreter &
SBCommandInterpreter::ref ()
{
    assert (m_opaque_ptr);
    return *m_opaque_ptr;
}

void
SBCommandInterpreter::reset (lldb_private::CommandInterpreter *interpreter)
{
    m_opaque_ptr = interpreter;
}

void
SBCommandInterpreter::SourceInitFileInHomeDirectory (SBCommandReturnObject &result)
{
    result.Clear();
    if (m_opaque_ptr)
    {
        TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
        Mutex::Locker api_locker;
        if (target_sp)
            api_locker.Reset(target_sp->GetAPIMutex().GetMutex());
        m_opaque_ptr->SourceInitFile (false, result.ref());
    }
    else
    {
        result->AppendError ("SBCommandInterpreter is not valid");
        result->SetStatus (eReturnStatusFailed);
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::SourceInitFileInHomeDirectory (&SBCommandReturnObject(%p))", 
                     m_opaque_ptr, result.get());

}

void
SBCommandInterpreter::SourceInitFileInCurrentWorkingDirectory (SBCommandReturnObject &result)
{
    result.Clear();
    if (m_opaque_ptr)
    {
        TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
        Mutex::Locker api_locker;
        if (target_sp)
            api_locker.Reset(target_sp->GetAPIMutex().GetMutex());
        m_opaque_ptr->SourceInitFile (true, result.ref());
    }
    else
    {
        result->AppendError ("SBCommandInterpreter is not valid");
        result->SetStatus (eReturnStatusFailed);
    }
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::SourceInitFileInCurrentWorkingDirectory (&SBCommandReturnObject(%p))", 
                     m_opaque_ptr, result.get());
}

SBBroadcaster
SBCommandInterpreter::GetBroadcaster ()
{
    LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_API));

    SBBroadcaster broadcaster (m_opaque_ptr, false);

    if (log)
        log->Printf ("SBCommandInterpreter(%p)::GetBroadcaster() => SBBroadcaster(%p)", 
                     m_opaque_ptr, broadcaster.get());

    return broadcaster;
}

const char * 
SBCommandInterpreter::GetArgumentTypeAsCString (const lldb::CommandArgumentType arg_type)
{
    return CommandObject::GetArgumentTypeAsCString (arg_type);
}

const char * 
SBCommandInterpreter::GetArgumentDescriptionAsCString (const lldb::CommandArgumentType arg_type)
{
    return CommandObject::GetArgumentDescriptionAsCString (arg_type);
}


#ifndef LLDB_DISABLE_PYTHON
extern "C" bool
LLDBSwigPythonBreakpointCallbackFunction 
(
    const char *python_function_name,
    const char *session_dictionary_name,
    const lldb::StackFrameSP& sb_frame, 
    const lldb::BreakpointLocationSP& sb_bp_loc
);

extern "C" std::string
LLDBSwigPythonCallTypeScript 
(
    const char *python_function_name,
    const char *session_dictionary_name,
    const lldb::ValueObjectSP& valobj_sp
);

extern "C" void*
LLDBSwigPythonCreateSyntheticProvider 
(
    const std::string python_class_name,
    const char *session_dictionary_name,
    const lldb::ValueObjectSP& valobj_sp
);


extern "C" uint32_t       LLDBSwigPython_CalculateNumChildren        (void *implementor);
extern "C" void*          LLDBSwigPython_GetChildAtIndex             (void *implementor, uint32_t idx);
extern "C" int            LLDBSwigPython_GetIndexOfChildWithName     (void *implementor, const char* child_name);
extern "C" void*          LLDBSWIGPython_CastPyObjectToSBValue       (void* data);
extern "C" void           LLDBSwigPython_UpdateSynthProviderInstance (void* implementor);

extern "C" bool           LLDBSwigPythonCallCommand 
(
    const char *python_function_name,
    const char *session_dictionary_name,
    lldb::DebuggerSP& debugger,
    const char* args,
    std::string& err_msg,
    lldb_private::CommandReturnObject& cmd_retobj
);

// Defined in the SWIG source file
extern "C" void 
init_lldb(void);

extern "C" bool           LLDBSwigPythonCallModuleInit 
(
    const std::string python_module_name,
    const char *session_dictionary_name,
    lldb::DebuggerSP& debugger
);

#else

extern "C" void init_lldb(void);

// Usually defined in the SWIG source file, but we have sripting disabled
extern "C" void 
init_lldb(void)
{
}

#endif

void
SBCommandInterpreter::InitializeSWIG ()
{
    static bool g_initialized = false;
    if (!g_initialized)
    {
        g_initialized = true;
#ifndef LLDB_DISABLE_PYTHON
        ScriptInterpreter::InitializeInterpreter (init_lldb, 
                                                  LLDBSwigPythonBreakpointCallbackFunction,
                                                  LLDBSwigPythonCallTypeScript,
                                                  LLDBSwigPythonCreateSyntheticProvider,
                                                  LLDBSwigPython_CalculateNumChildren,
                                                  LLDBSwigPython_GetChildAtIndex,
                                                  LLDBSwigPython_GetIndexOfChildWithName,
                                                  LLDBSWIGPython_CastPyObjectToSBValue,
                                                  LLDBSwigPython_UpdateSynthProviderInstance,
                                                  LLDBSwigPythonCallCommand,
                                                  LLDBSwigPythonCallModuleInit);
#endif
    }
}
