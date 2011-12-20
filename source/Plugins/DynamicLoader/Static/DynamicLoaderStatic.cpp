//===-- DynamicLoaderStatic.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/Target.h"

#include "DynamicLoaderStatic.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// Create an instance of this class. This function is filled into
// the plugin info class that gets handed out by the plugin factory and
// allows the lldb to instantiate an instance of this class.
//----------------------------------------------------------------------
DynamicLoader *
DynamicLoaderStatic::CreateInstance (Process* process, bool force)
{
    bool create = force;
    if (!create)
    {
        const llvm::Triple &triple_ref = process->GetTarget().GetArchitecture().GetTriple();
        const llvm::Triple::OSType os_type = triple_ref.getOS();
        if ((os_type == llvm::Triple::UnknownOS))
            create = true;
    }
    
    if (create)
        return new DynamicLoaderStatic (process);
    return NULL;
}

//----------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------
DynamicLoaderStatic::DynamicLoaderStatic (Process* process) :
    DynamicLoader(process)
{
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
DynamicLoaderStatic::~DynamicLoaderStatic()
{
}

//------------------------------------------------------------------
/// Called after attaching a process.
///
/// Allow DynamicLoader plug-ins to execute some code after
/// attaching to a process.
//------------------------------------------------------------------
void
DynamicLoaderStatic::DidAttach ()
{
    LoadAllImagesAtFileAddresses();
}

//------------------------------------------------------------------
/// Called after attaching a process.
///
/// Allow DynamicLoader plug-ins to execute some code after
/// attaching to a process.
//------------------------------------------------------------------
void
DynamicLoaderStatic::DidLaunch ()
{
    LoadAllImagesAtFileAddresses();
}

void
DynamicLoaderStatic::LoadAllImagesAtFileAddresses ()
{
    ModuleList &module_list = m_process->GetTarget().GetImages();
    
    ModuleList loaded_module_list;

    const size_t num_modules = module_list.GetSize();
    for (uint32_t idx = 0; idx < num_modules; ++idx)
    {
        ModuleSP module_sp (module_list.GetModuleAtIndex (idx));
        if (module_sp)
        {
            bool changed = false;
            ObjectFile *image_object_file = module_sp->GetObjectFile();
            if (image_object_file)
            {
                SectionList *section_list = image_object_file->GetSectionList ();
                if (section_list)
                {
                    // All sections listed in the dyld image info structure will all
                    // either be fixed up already, or they will all be off by a single
                    // slide amount that is determined by finding the first segment
                    // that is at file offset zero which also has bytes (a file size
                    // that is greater than zero) in the object file.

                    // Determine the slide amount (if any)
                    const size_t num_sections = section_list->GetSize();
                    size_t sect_idx = 0;
                    for (sect_idx = 0; sect_idx < num_sections; ++sect_idx)
                    {
                        // Iterate through the object file sections to find the
                        // first section that starts of file offset zero and that
                        // has bytes in the file...
                        Section *section = section_list->GetSectionAtIndex (sect_idx).get();
                        if (section)
                        {
                            if (m_process->GetTarget().GetSectionLoadList().SetSectionLoadAddress (section, section->GetFileAddress()))
                                changed = true;
                        }
                    }
                }
            }
            
            if (changed)
                loaded_module_list.AppendIfNeeded (module_sp);
        }
    }

    if (loaded_module_list.GetSize())
        m_process->GetTarget().ModulesDidLoad (loaded_module_list);
}

ThreadPlanSP
DynamicLoaderStatic::GetStepThroughTrampolinePlan (Thread &thread, bool stop_others)
{
    return ThreadPlanSP();
}

Error
DynamicLoaderStatic::CanLoadImage ()
{
    Error error;
    error.SetErrorString ("can't load images on with a static debug session");
    return error;
}

void
DynamicLoaderStatic::Initialize()
{
    PluginManager::RegisterPlugin (GetPluginNameStatic(),
                                   GetPluginDescriptionStatic(),
                                   CreateInstance);
}

void
DynamicLoaderStatic::Terminate()
{
    PluginManager::UnregisterPlugin (CreateInstance);
}


const char *
DynamicLoaderStatic::GetPluginNameStatic()
{
    return "dynamic-loader.static";
}

const char *
DynamicLoaderStatic::GetPluginDescriptionStatic()
{
    return "Dynamic loader plug-in that will load any images at the static addresses contained in each image.";
}


//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------
const char *
DynamicLoaderStatic::GetPluginName()
{
    return "DynamicLoaderStatic";
}

const char *
DynamicLoaderStatic::GetShortPluginName()
{
    return GetPluginNameStatic();
}

uint32_t
DynamicLoaderStatic::GetPluginVersion()
{
    return 1;
}
