//===-- ClangASTSource.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "clang/AST/ASTContext.h"
#include "lldb/Core/Log.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Expression/ASTDumper.h"
#include "lldb/Expression/ClangASTSource.h"
#include "lldb/Expression/ClangExpression.h"
#include "lldb/Symbol/ClangNamespaceDecl.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/Target/Target.h"

using namespace clang;
using namespace lldb_private;

ClangASTSource::~ClangASTSource() 
{
    m_ast_importer->ForgetDestination(m_ast_context);
    
    // We are in the process of destruction, don't create clang ast context on demand
    // by passing false to Target::GetScratchClangASTContext(create_on_demand).
    ClangASTContext *scratch_clang_ast_context = m_target->GetScratchClangASTContext(false);
    
    if (!scratch_clang_ast_context)
        return;
    
    clang::ASTContext *scratch_ast_context = scratch_clang_ast_context->getASTContext();
    
    if (!scratch_ast_context)
        return;
    
    if (m_ast_context != scratch_ast_context)
        m_ast_importer->ForgetSource(scratch_ast_context, m_ast_context);
}

void
ClangASTSource::StartTranslationUnit(ASTConsumer *Consumer) 
{
    if (!m_ast_context)
        return;
    
    m_ast_context->getTranslationUnitDecl()->setHasExternalVisibleStorage();
    m_ast_context->getTranslationUnitDecl()->setHasExternalLexicalStorage();
}

// The core lookup interface.
DeclContext::lookup_result 
ClangASTSource::FindExternalVisibleDeclsByName
(
    const DeclContext *decl_ctx, 
    DeclarationName clang_decl_name
) 
{
    if (!m_ast_context)
        return SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
    
    if (GetImportInProgress())
        return SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
        
    std::string decl_name (clang_decl_name.getAsString());

//    if (m_decl_map.DoingASTImport ())
//      return DeclContext::lookup_result();
//        
    switch (clang_decl_name.getNameKind()) {
    // Normal identifiers.
    case DeclarationName::Identifier:
        if (clang_decl_name.getAsIdentifierInfo()->getBuiltinID() != 0)
            return SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
        break;
            
    // Operator names.  Not important for now.
    case DeclarationName::CXXOperatorName:
    case DeclarationName::CXXLiteralOperatorName:
      return DeclContext::lookup_result();
            
    // Using directives found in this context.
    // Tell Sema we didn't find any or we'll end up getting asked a *lot*.
    case DeclarationName::CXXUsingDirective:
      return SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
            
    case DeclarationName::ObjCZeroArgSelector:
    case DeclarationName::ObjCOneArgSelector:
    case DeclarationName::ObjCMultiArgSelector:
    {
      llvm::SmallVector<NamedDecl*, 1> method_decls;    

      NameSearchContext method_search_context (*this, method_decls, clang_decl_name, decl_ctx);
     
      FindObjCMethodDecls(method_search_context);

      return SetExternalVisibleDeclsForName (decl_ctx, clang_decl_name, method_decls);
    }
    // These aren't possible in the global context.
    case DeclarationName::CXXConstructorName:
    case DeclarationName::CXXDestructorName:
    case DeclarationName::CXXConversionFunctionName:
      return DeclContext::lookup_result();
    }


    if (!GetLookupsEnabled())
    {
        // Wait until we see a '$' at the start of a name before we start doing 
        // any lookups so we can avoid lookup up all of the builtin types.
        if (!decl_name.empty() && decl_name[0] == '$')
        {
            SetLookupsEnabled (true);
        }
        else
        {               
            return SetNoExternalVisibleDeclsForName(decl_ctx, clang_decl_name);
        }
    }

    ConstString const_decl_name(decl_name.c_str());
    
    const char *uniqued_const_decl_name = const_decl_name.GetCString();
    if (m_active_lookups.find (uniqued_const_decl_name) != m_active_lookups.end())
    {
        // We are currently looking up this name...
        return DeclContext::lookup_result();
    }
    m_active_lookups.insert(uniqued_const_decl_name);
//  static uint32_t g_depth = 0;
//  ++g_depth;
//  printf("[%5u] FindExternalVisibleDeclsByName() \"%s\"\n", g_depth, uniqued_const_decl_name);
    llvm::SmallVector<NamedDecl*, 4> name_decls;    
    NameSearchContext name_search_context(*this, name_decls, clang_decl_name, decl_ctx);
    FindExternalVisibleDecls(name_search_context);
    DeclContext::lookup_result result (SetExternalVisibleDeclsForName (decl_ctx, clang_decl_name, name_decls));
//  --g_depth;
    m_active_lookups.erase (uniqued_const_decl_name);
    return result;
}

void
ClangASTSource::CompleteType (TagDecl *tag_decl)
{    
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;
    
    if (log)
    {
        log->Printf("    CompleteTagDecl[%u] on (ASTContext*)%p Completing a TagDecl named %s", 
                    invocation_id, 
                    m_ast_context, 
                    tag_decl->getName().str().c_str());
        
        log->Printf("      CTD[%u] Before:", current_id);
        ASTDumper dumper((Decl*)tag_decl);
        dumper.ToLog(log, "      [CTD] ");
    }
    
    if (!m_ast_importer->CompleteTagDecl (tag_decl))
    {
        // We couldn't complete the type.  Maybe there's a definition
        // somewhere else that can be completed.
        
        if (log)
            log->Printf("      CTD[%u] Type could not be completed in the module in which it was first found.", current_id);
        
        bool found = false;

        DeclContext *decl_ctx = tag_decl->getDeclContext();
                
        if (const NamespaceDecl *namespace_context = dyn_cast<NamespaceDecl>(decl_ctx))
        {
            ClangASTImporter::NamespaceMapSP namespace_map = m_ast_importer->GetNamespaceMap(namespace_context);
            
            if (log && log->GetVerbose())
                log->Printf("      CTD[%u] Inspecting namespace map %p (%d entries)", 
                            current_id, 
                            namespace_map.get(), 
                            (int)namespace_map->size());
            
            if (!namespace_map)
                return;
            
            for (ClangASTImporter::NamespaceMap::iterator i = namespace_map->begin(), e = namespace_map->end();
                 i != e && !found;
                 ++i)
            {
                if (log)
                    log->Printf("      CTD[%u] Searching namespace %s in module %s",
                                current_id,
                                i->second.GetNamespaceDecl()->getNameAsString().c_str(),
                                i->first->GetFileSpec().GetFilename().GetCString());
                
                TypeList types;
                
                SymbolContext null_sc;
                ConstString name(tag_decl->getName().str().c_str());
                
                i->first->FindTypes(null_sc, name, &i->second, true, UINT32_MAX, types);
                
                for (uint32_t ti = 0, te = types.GetSize();
                     ti != te && !found;
                     ++ti)
                {
                    lldb::TypeSP type = types.GetTypeAtIndex(ti);
                    
                    if (!type)
                        continue;
                    
                    lldb::clang_type_t opaque_type = type->GetClangFullType();
                    
                    if (!opaque_type)
                        continue;
                    
                    const TagType *tag_type = QualType::getFromOpaquePtr(opaque_type)->getAs<TagType>();
                    
                    if (!tag_type)
                        continue;
                    
                    TagDecl *candidate_tag_decl = const_cast<TagDecl*>(tag_type->getDecl());
                    
                    if (m_ast_importer->CompleteTagDeclWithOrigin (tag_decl, candidate_tag_decl))
                        found = true;
                }
            }
        }
        else 
        {
            TypeList types;
            
            SymbolContext null_sc;
            ConstString name(tag_decl->getName().str().c_str());
            ClangNamespaceDecl namespace_decl;
            
            ModuleList &module_list = m_target->GetImages();

            module_list.FindTypes(null_sc, name, true, UINT32_MAX, types);
            
            for (uint32_t ti = 0, te = types.GetSize();
                 ti != te && !found;
                 ++ti)
            {
                lldb::TypeSP type = types.GetTypeAtIndex(ti);
                
                if (!type)
                    continue;
                
                lldb::clang_type_t opaque_type = type->GetClangFullType();
                
                if (!opaque_type)
                    continue;
                
                const TagType *tag_type = QualType::getFromOpaquePtr(opaque_type)->getAs<TagType>();
                
                if (!tag_type)
                    continue;
                
                TagDecl *candidate_tag_decl = const_cast<TagDecl*>(tag_type->getDecl());
                
                if (m_ast_importer->CompleteTagDeclWithOrigin (tag_decl, candidate_tag_decl))
                    found = true;
            }
        }
    }
    
    if (log)
    {
        log->Printf("      [CTD] After:");
        ASTDumper dumper((Decl*)tag_decl);
        dumper.ToLog(log, "      [CTD] ");
    }
}

void
ClangASTSource::CompleteType (clang::ObjCInterfaceDecl *interface_decl)
{    
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    if (log)
    {
        log->Printf("    [CompleteObjCInterfaceDecl] on (ASTContext*)%p Completing an ObjCInterfaceDecl named %s", m_ast_context, interface_decl->getName().str().c_str());
        log->Printf("      [COID] Before:");
        ASTDumper dumper((Decl*)interface_decl);
        dumper.ToLog(log, "      [COID] ");    
    }
    
    m_ast_importer->CompleteObjCInterfaceDecl (interface_decl);
    
    if (log)
    {
        log->Printf("      [COID] After:");
        ASTDumper dumper((Decl*)interface_decl);
        dumper.ToLog(log, "      [COID] ");    
    }
}

clang::ExternalLoadResult
ClangASTSource::FindExternalLexicalDecls (const DeclContext *decl_context, 
                                          bool (*predicate)(Decl::Kind),
                                          llvm::SmallVectorImpl<Decl*> &decls)
{    
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    const Decl *context_decl = dyn_cast<Decl>(decl_context);
    
    if (!context_decl)
        return ELR_Failure;
    
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;
    
    if (log)
    {
        if (const NamedDecl *context_named_decl = dyn_cast<NamedDecl>(context_decl))
            log->Printf("FindExternalLexicalDecls[%u] on (ASTContext*)%p in '%s' (%sDecl*)%p with %s predicate",
                        current_id,
                        m_ast_context,
                        context_named_decl->getNameAsString().c_str(),
                        context_decl->getDeclKindName(),
                        context_decl,
                        (predicate ? "non-null" : "null"));
        else if(context_decl)
            log->Printf("FindExternalLexicalDecls[%u] on (ASTContext*)%p in (%sDecl*)%p with %s predicate",
                        current_id,
                        m_ast_context,
                        context_decl->getDeclKindName(), 
                        context_decl,
                        (predicate ? "non-null" : "null"));
        else
            log->Printf("FindExternalLexicalDecls[%u] on (ASTContext*)%p in a NULL context with %s predicate",
                        current_id,
                        m_ast_context,
                        (predicate ? "non-null" : "null"));
    }
    
    Decl *original_decl = NULL;
    ASTContext *original_ctx = NULL;
    
    if (!m_ast_importer->ResolveDeclOrigin(context_decl, &original_decl, &original_ctx))
        return ELR_Failure;
    
    if (log)
    {       
        log->Printf("  FELD[%u] Original decl:", current_id);
        ASTDumper(original_decl).ToLog(log, "    ");
    }
    
    if (TagDecl *original_tag_decl = dyn_cast<TagDecl>(original_decl))
    {
        ExternalASTSource *external_source = original_ctx->getExternalSource();
        
        if (external_source)
            external_source->CompleteType (original_tag_decl);
    }
    
    const DeclContext *original_decl_context = dyn_cast<DeclContext>(original_decl);
    
    if (!original_decl_context)
        return ELR_Failure;
    
    for (TagDecl::decl_iterator iter = original_decl_context->decls_begin();
         iter != original_decl_context->decls_end();
         ++iter)
    {
        Decl *decl = *iter;
        
        if (!predicate || predicate(decl->getKind()))
        {
            if (log)
            {
                ASTDumper ast_dumper(decl);
                if (const NamedDecl *context_named_decl = dyn_cast<NamedDecl>(context_decl))
                    log->Printf("  FELD[%d] Adding [to %s] lexical decl %s", current_id, context_named_decl->getNameAsString().c_str(), ast_dumper.GetCString());
                else
                    log->Printf("  FELD[%d] Adding lexical decl %s", current_id, ast_dumper.GetCString());
            }
            
            Decl *copied_decl = m_ast_importer->CopyDecl(m_ast_context, original_ctx, decl);
            
            decls.push_back(copied_decl);
        }
    }
    
    return ELR_AlreadyLoaded;
}

void
ClangASTSource::FindExternalVisibleDecls (NameSearchContext &context)
{
    assert (m_ast_context);
    
    const ConstString name(context.m_decl_name.getAsString().c_str());
    
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;
    
    if (log)
    {
        if (!context.m_decl_context)
            log->Printf("ClangASTSource::FindExternalVisibleDecls[%u] on (ASTContext*)%p for '%s' in a NULL DeclContext", current_id,                         m_ast_context, name.GetCString());
        else if (const NamedDecl *context_named_decl = dyn_cast<NamedDecl>(context.m_decl_context))
            log->Printf("ClangASTSource::FindExternalVisibleDecls[%u] on (ASTContext*)%p for '%s' in '%s'", current_id, m_ast_context, name.GetCString(), context_named_decl->getNameAsString().c_str());
        else
            log->Printf("ClangASTSource::FindExternalVisibleDecls[%u] on (ASTContext*)%p for '%s' in a '%s'", current_id, m_ast_context, name.GetCString(), context.m_decl_context->getDeclKindName());
    }
    
    context.m_namespace_map.reset(new ClangASTImporter::NamespaceMap);
    
    if (const NamespaceDecl *namespace_context = dyn_cast<NamespaceDecl>(context.m_decl_context))
    {
        ClangASTImporter::NamespaceMapSP namespace_map = m_ast_importer->GetNamespaceMap(namespace_context);
        
        if (log && log->GetVerbose())
            log->Printf("  CAS::FEVD[%u] Inspecting namespace map %p (%d entries)", 
                        current_id, 
                        namespace_map.get(), 
                        (int)namespace_map->size());
        
        if (!namespace_map)
            return;
        
        for (ClangASTImporter::NamespaceMap::iterator i = namespace_map->begin(), e = namespace_map->end();
             i != e;
             ++i)
        {
            if (log)
                log->Printf("  CAS::FEVD[%u] Searching namespace %s in module %s",
                            current_id,
                            i->second.GetNamespaceDecl()->getNameAsString().c_str(),
                            i->first->GetFileSpec().GetFilename().GetCString());
            
            FindExternalVisibleDecls(context,
                                     i->first,
                                     i->second,
                                     current_id);
        }
    }
    else if (isa<ObjCInterfaceDecl>(context.m_decl_context))
    {
        FindObjCPropertyDecls(context);
    }
    else if (!isa<TranslationUnitDecl>(context.m_decl_context))
    {
        // we shouldn't be getting FindExternalVisibleDecls calls for these
        return;
    }
    else
    {
        ClangNamespaceDecl namespace_decl;
        
        if (log)
            log->Printf("  CAS::FEVD[%u] Searching the root namespace", current_id);
        
        FindExternalVisibleDecls(context,
                                 lldb::ModuleSP(),
                                 namespace_decl,
                                 current_id);
    }
    
    if (!context.m_namespace_map->empty())
    {
        if (log && log->GetVerbose())
            log->Printf("  CAS::FEVD[%u] Registering namespace map %p (%d entries)", 
                        current_id,
                        context.m_namespace_map.get(), 
                        (int)context.m_namespace_map->size());
        
        NamespaceDecl *clang_namespace_decl = AddNamespace(context, context.m_namespace_map);
        
        if (clang_namespace_decl)
            clang_namespace_decl->setHasExternalVisibleStorage();
    }
}

void 
ClangASTSource::FindExternalVisibleDecls (NameSearchContext &context, 
                                          lldb::ModuleSP module_sp,
                                          ClangNamespaceDecl &namespace_decl,
                                          unsigned int current_id)
{
    assert (m_ast_context);
    
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    SymbolContextList sc_list;
    
    const ConstString name(context.m_decl_name.getAsString().c_str());
    
    const char *name_unique_cstr = name.GetCString();
    
    if (name_unique_cstr == NULL)
        return;
    
    // The ClangASTSource is not responsible for finding $-names.
    if (name_unique_cstr[0] == '$')
        return;
    
    if (module_sp && namespace_decl)
    {
        ClangNamespaceDecl found_namespace_decl;
        
        SymbolVendor *symbol_vendor = module_sp->GetSymbolVendor();
        
        if (symbol_vendor)
        {
            SymbolContext null_sc;
            
            found_namespace_decl = symbol_vendor->FindNamespace(null_sc, name, &namespace_decl);
            
            if (found_namespace_decl)
            {
                context.m_namespace_map->push_back(std::pair<lldb::ModuleSP, ClangNamespaceDecl>(module_sp, found_namespace_decl));
                
                if (log)
                    log->Printf("  CAS::FEVD[%u] Found namespace %s in module %s",
                                current_id,
                                name.GetCString(), 
                                module_sp->GetFileSpec().GetFilename().GetCString());
            }
        }
    }
    else 
    {
        ModuleList &images = m_target->GetImages();
        
        for (uint32_t i = 0, e = images.GetSize();
             i != e;
             ++i)
        {
            lldb::ModuleSP image = images.GetModuleAtIndex(i);
            
            if (!image)
                continue;
            
            ClangNamespaceDecl found_namespace_decl;
            
            SymbolVendor *symbol_vendor = image->GetSymbolVendor();
            
            if (!symbol_vendor)
                continue;
            
            SymbolContext null_sc;
            
            found_namespace_decl = symbol_vendor->FindNamespace(null_sc, name, &namespace_decl);
            
            if (found_namespace_decl)
            {
                context.m_namespace_map->push_back(std::pair<lldb::ModuleSP, ClangNamespaceDecl>(image, found_namespace_decl));
                
                if (log)
                    log->Printf("  CAS::FEVD[%u] Found namespace %s in module %s",
                                current_id,
                                name.GetCString(), 
                                image->GetFileSpec().GetFilename().GetCString());
            }
        }
    }
    
    static ConstString id_name("id");
    static ConstString Class_name("Class");
    
    do 
    {
        TypeList types;
        SymbolContext null_sc;
        
        if (name == id_name || name == Class_name)
            break;
      
        if (module_sp && namespace_decl)
            module_sp->FindTypes(null_sc, name, &namespace_decl, true, 1, types);
        else 
            m_target->GetImages().FindTypes(null_sc, name, true, 1, types);
        
        if (types.GetSize())
        {
            lldb::TypeSP type_sp = types.GetTypeAtIndex(0);
            
            if (log)
            {
                const char *name_string = type_sp->GetName().GetCString();
                
                log->Printf("  CAS::FEVD[%u] Matching type found for \"%s\": %s", 
                            current_id, 
                            name.GetCString(), 
                            (name_string ? name_string : "<anonymous>"));
            }
            

            void *copied_type = GuardedCopyType(m_ast_context, type_sp->GetClangAST(), type_sp->GetClangFullType());
                
            if (!copied_type)
            {                
                if (log)
                    log->Printf("  CAS::FEVD[%u] - Couldn't export the type for a constant integer result",
                                current_id);
                    
                break;
            }
                
            context.AddTypeDecl(copied_type);
        }
        
    } while(0);
}

void
ClangASTSource::FindObjCMethodDecls (NameSearchContext &context)
{
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;
    
    const DeclarationName &decl_name(context.m_decl_name);
    const DeclContext *decl_ctx(context.m_decl_context);
    
    const ObjCInterfaceDecl *interface_decl = dyn_cast<ObjCInterfaceDecl>(decl_ctx);
    
    if (!interface_decl)
        return;
    
    StreamString ss;
        
    if (decl_name.isObjCZeroArgSelector())
    {
        ss.Printf("%s", decl_name.getAsString().c_str());
    }
    else if (decl_name.isObjCOneArgSelector())
    {
        ss.Printf("%s", decl_name.getAsString().c_str());
    }
    else
    {    
        clang::Selector sel = decl_name.getObjCSelector();
        
        for (unsigned i = 0, e = sel.getNumArgs();
             i != e;
             ++i)
        {
            llvm::StringRef r = sel.getNameForSlot(i);
            ss.Printf("%s:", r.str().c_str()); 
        }
    }     
    ss.Flush();
    
    ConstString selector_name(ss.GetData());
    
    if (log)
        log->Printf("ClangASTSource::FindObjCMethodDecls[%d] on (ASTContext*)%p for selector [%s %s]",
                    current_id,
                    m_ast_context,
                    interface_decl->getNameAsString().c_str(), 
                    selector_name.AsCString());

    ClangASTImporter::ObjCInterfaceMapSP interface_map = m_ast_importer->GetObjCInterfaceMap(interface_decl);
    
    if (interface_map)
    {
        for (ClangASTImporter::ObjCInterfaceMap::iterator i = interface_map->begin(), e = interface_map->end();
             i != e;
             ++i)
        {
            lldb::clang_type_t backing_type = i->GetOpaqueQualType();
            
            if (!backing_type)
                continue;
            
            QualType backing_qual_type = QualType::getFromOpaquePtr(backing_type);
            
            const ObjCInterfaceType *backing_interface_type = backing_qual_type.getTypePtr()->getAs<ObjCInterfaceType>();
            
            if (!backing_interface_type)
                continue;
            
            const ObjCInterfaceDecl *backing_interface_decl = backing_interface_type->getDecl();
            
            if (!backing_interface_decl)
                continue;
            
            if (backing_interface_decl->decls_begin() == backing_interface_decl->decls_end())
                continue; // don't waste time creating a DeclarationName here
            
            clang::ASTContext &backing_ast_context = backing_interface_decl->getASTContext();
            
            llvm::SmallVector<clang::IdentifierInfo *, 3> selector_components;
            int num_arguments = 0;

            if (decl_name.isObjCZeroArgSelector())
            {
                selector_components.push_back (&backing_ast_context.Idents.get(decl_name.getAsString().c_str()));
            }
            else if (decl_name.isObjCOneArgSelector())
            {
                selector_components.push_back (&backing_ast_context.Idents.get(decl_name.getAsString().c_str()));
                num_arguments = 1;
            }
            else
            {    
                clang::Selector sel = decl_name.getObjCSelector();
                
                for (unsigned i = 0, e = sel.getNumArgs();
                     i != e;
                     ++i)
                {
                    llvm::StringRef r = sel.getNameForSlot(i);

                    selector_components.push_back (&backing_ast_context.Idents.get(r.str().c_str()));
                    num_arguments++;
                }
            }     
            
            Selector backing_selector = backing_interface_decl->getASTContext().Selectors.getSelector(num_arguments, selector_components.data());
            DeclarationName backing_decl_name = DeclarationName(backing_selector);
            
            DeclContext::lookup_const_result lookup_result = backing_interface_decl->lookup(backing_decl_name);
            
            if (lookup_result.first == lookup_result.second)
                continue;
            
            ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(*lookup_result.first);
            
            if (!method_decl)
                continue;
            
            Decl *copied_decl = m_ast_importer->CopyDecl(m_ast_context, &backing_ast_context, *lookup_result.first);
            
            if (!copied_decl)
            {
                if (log)
                    log->Printf("  CAS::FOMD[%d] couldn't import method from symbols", current_id);
                continue;
            }
            
            ObjCMethodDecl *copied_method_decl = dyn_cast<ObjCMethodDecl> (copied_decl);
            
            if (!copied_method_decl)
                continue;
            
            if (log)
            {
                ASTDumper dumper((Decl*)copied_method_decl);
                log->Printf("  CAS::FOMD[%d] found (in symbols) %s", current_id, dumper.GetCString());
            }
            
            context.AddNamedDecl(copied_method_decl);
        }
    }
    
    SymbolContextList sc_list;
    
    const bool include_symbols = false;
    const bool append = false;
    
    m_target->GetImages().FindFunctions(selector_name, lldb::eFunctionNameTypeSelector, include_symbols, append, sc_list);
    
    for (uint32_t i = 0, e = sc_list.GetSize();
         i != e;
         ++i)
    {
        SymbolContext sc;
        
        if (!sc_list.GetContextAtIndex(i, sc))
            continue;
        
        if (!sc.function)
            continue;
        
        DeclContext *function_ctx = sc.function->GetClangDeclContext();
        
        if (!function_ctx)
            continue;
        
        ObjCMethodDecl *method_decl = dyn_cast<ObjCMethodDecl>(function_ctx);
        
        if (!method_decl)
            continue;
        
        ObjCInterfaceDecl *found_interface_decl = method_decl->getClassInterface();
        
        if (!found_interface_decl)
            continue;
        
        if (found_interface_decl->getName() == interface_decl->getName())
        {
            Decl *copied_decl = m_ast_importer->CopyDecl(m_ast_context, &method_decl->getASTContext(), method_decl);
            
            if (!copied_decl)
                continue;
            
            ObjCMethodDecl *copied_method_decl = dyn_cast<ObjCMethodDecl>(copied_decl);
            
            if (!copied_method_decl)
                continue;
            
            if (log)
            {
                ASTDumper dumper((Decl*)copied_method_decl);
                log->Printf("  CAS::FOMD[%d] found (in debug info) %s", current_id, dumper.GetCString());
            }
                
            context.AddNamedDecl(copied_method_decl);
        }
    }
}

void
ClangASTSource::FindObjCPropertyDecls (NameSearchContext &context)
{
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;
    
    const ObjCInterfaceDecl *iface_decl = cast<ObjCInterfaceDecl>(context.m_decl_context);
    Decl *orig_decl;
    ASTContext *orig_ast_ctx;
    
    m_ast_importer->ResolveDeclOrigin(iface_decl, &orig_decl, &orig_ast_ctx);
    
    if (!orig_decl)
        return;
    
    ObjCInterfaceDecl *orig_iface_decl = dyn_cast<ObjCInterfaceDecl>(orig_decl);
    
    if (!orig_iface_decl)
        return;
    
    if (!ClangASTContext::GetCompleteDecl(orig_ast_ctx, orig_iface_decl))
        return;
    
    std::string property_name_str = context.m_decl_name.getAsString();
    StringRef property_name(property_name_str.c_str());
    ObjCPropertyDecl *property_decl = orig_iface_decl->FindPropertyDeclaration(&orig_ast_ctx->Idents.get(property_name));
    
    if (log)
        log->Printf("ClangASTSource::FindObjCPropertyDecls[%d] on (ASTContext*)%p for property '%s.%s'",
                    current_id, 
                    m_ast_context,
                    iface_decl->getNameAsString().c_str(), 
                    property_name_str.c_str());
    
    if (!property_decl)
        return;
    
    Decl *copied_decl = m_ast_importer->CopyDecl(m_ast_context, orig_ast_ctx, property_decl);
    
    if (!copied_decl)
        return;
    
    ObjCPropertyDecl *copied_property_decl = dyn_cast<ObjCPropertyDecl>(copied_decl);
    
    if (!copied_property_decl)
        return;
    
    if (log)
    {
        ASTDumper dumper((Decl*)copied_property_decl);
        log->Printf("  CAS::FOPD[%d] found %s", current_id, dumper.GetCString());
    }
 
    context.AddNamedDecl(copied_property_decl);
}

void 
ClangASTSource::CompleteNamespaceMap (ClangASTImporter::NamespaceMapSP &namespace_map,
                                      const ConstString &name,
                                      ClangASTImporter::NamespaceMapSP &parent_map) const
{
    static unsigned int invocation_id = 0;
    unsigned int current_id = invocation_id++;
    
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
    
    if (log)
    {
        if (parent_map && parent_map->size())
            log->Printf("CompleteNamespaceMap[%u] on (ASTContext*)%p Searching for namespace %s in namespace %s",
                        current_id,
                        m_ast_context,
                        name.GetCString(),
                        parent_map->begin()->second.GetNamespaceDecl()->getDeclName().getAsString().c_str());
        else
            log->Printf("CompleteNamespaceMap[%u] on (ASTContext*)%p Searching for namespace %s",
                        current_id,
                        m_ast_context,
                        name.GetCString());
    }
    
    
    if (parent_map)
    {
        for (ClangASTImporter::NamespaceMap::iterator i = parent_map->begin(), e = parent_map->end();
             i != e;
             ++i)
        {
            ClangNamespaceDecl found_namespace_decl;
            
            lldb::ModuleSP module_sp = i->first;
            ClangNamespaceDecl module_parent_namespace_decl = i->second;
            
            SymbolVendor *symbol_vendor = module_sp->GetSymbolVendor();
            
            if (!symbol_vendor)
                continue;
            
            SymbolContext null_sc;
            
            found_namespace_decl = symbol_vendor->FindNamespace(null_sc, name, &module_parent_namespace_decl);
            
            if (!found_namespace_decl)
                continue;
            
            namespace_map->push_back(std::pair<lldb::ModuleSP, ClangNamespaceDecl>(module_sp, found_namespace_decl));
            
            if (log)
                log->Printf("  CMN[%u] Found namespace %s in module %s",
                            current_id,
                            name.GetCString(), 
                            module_sp->GetFileSpec().GetFilename().GetCString());
        }
    }
    else
    {
        ModuleList &images = m_target->GetImages();
        ClangNamespaceDecl null_namespace_decl;
        
        for (uint32_t i = 0, e = images.GetSize();
             i != e;
             ++i)
        {
            lldb::ModuleSP image = images.GetModuleAtIndex(i);
            
            if (!image)
                continue;
            
            ClangNamespaceDecl found_namespace_decl;
            
            SymbolVendor *symbol_vendor = image->GetSymbolVendor();
            
            if (!symbol_vendor)
                continue;
            
            SymbolContext null_sc;
            
            found_namespace_decl = symbol_vendor->FindNamespace(null_sc, name, &null_namespace_decl);
            
            if (!found_namespace_decl)
                continue;
            
            namespace_map->push_back(std::pair<lldb::ModuleSP, ClangNamespaceDecl>(image, found_namespace_decl));
            
            if (log)
                log->Printf("  CMN[%u] Found namespace %s in module %s",
                            current_id,
                            name.GetCString(), 
                            image->GetFileSpec().GetFilename().GetCString());
        }
    }
}

void
ClangASTSource::CompleteObjCInterfaceMap (ClangASTImporter::ObjCInterfaceMapSP &objc_interface_map,
                                          const ConstString &name) const
{
    SymbolContext null_sc;
    
    TypeList types;
    
    m_target->GetImages().FindTypes(null_sc, name, true, UINT32_MAX, types);
    
    for (uint32_t i = 0, e = types.GetSize();
         i != e;
         ++i)
    {
        lldb::TypeSP mapped_type_sp = types.GetTypeAtIndex(i);
        
        if (!mapped_type_sp || !mapped_type_sp->GetClangFullType())
            continue;
        
        objc_interface_map->push_back (ClangASTType(mapped_type_sp->GetClangAST(), mapped_type_sp->GetClangFullType()));
    }
}

NamespaceDecl *
ClangASTSource::AddNamespace (NameSearchContext &context, ClangASTImporter::NamespaceMapSP &namespace_decls)
{
    if (namespace_decls.empty())
        return NULL;
    
    lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));
        
    const ClangNamespaceDecl &namespace_decl = namespace_decls->begin()->second;
    
    Decl *copied_decl = m_ast_importer->CopyDecl(m_ast_context, namespace_decl.GetASTContext(), namespace_decl.GetNamespaceDecl());
    
    NamespaceDecl *copied_namespace_decl = dyn_cast<NamespaceDecl>(copied_decl);
    
    m_ast_importer->RegisterNamespaceMap(copied_namespace_decl, namespace_decls);
    
    return dyn_cast<NamespaceDecl>(copied_decl);
}

void * 
ClangASTSource::GuardedCopyType (ASTContext *dest_context, 
                                 ASTContext *source_context,
                                 void *clang_type)
{    
    SetImportInProgress(true);
    
    QualType ret_qual_type = m_ast_importer->CopyType (m_ast_context, source_context, QualType::getFromOpaquePtr(clang_type));
    
    void *ret = ret_qual_type.getAsOpaquePtr();
    
    SetImportInProgress(false);
    
    return ret;
}

clang::NamedDecl *
NameSearchContext::AddVarDecl(void *type) 
{
    IdentifierInfo *ii = m_decl_name.getAsIdentifierInfo();
    
    assert (type && "Type for variable must be non-NULL!");
        
    clang::NamedDecl *Decl = VarDecl::Create(*m_ast_source.m_ast_context, 
                                             const_cast<DeclContext*>(m_decl_context), 
                                             SourceLocation(), 
                                             SourceLocation(),
                                             ii, 
                                             QualType::getFromOpaquePtr(type), 
                                             0, 
                                             SC_Static, 
                                             SC_Static);
    m_decls.push_back(Decl);
    
    return Decl;
}

clang::NamedDecl *
NameSearchContext::AddFunDecl (void *type) 
{
    clang::FunctionDecl *func_decl = FunctionDecl::Create (*m_ast_source.m_ast_context,
                                                           const_cast<DeclContext*>(m_decl_context),
                                                           SourceLocation(),
                                                           SourceLocation(),
                                                           m_decl_name.getAsIdentifierInfo(),
                                                           QualType::getFromOpaquePtr(type),
                                                           NULL,
                                                           SC_Static,
                                                           SC_Static,
                                                           false,
                                                           true);
    
    // We have to do more than just synthesize the FunctionDecl.  We have to
    // synthesize ParmVarDecls for all of the FunctionDecl's arguments.  To do
    // this, we raid the function's FunctionProtoType for types.
    
    QualType qual_type (QualType::getFromOpaquePtr(type));
    const FunctionProtoType *func_proto_type = qual_type.getTypePtr()->getAs<FunctionProtoType>();
    
    if (func_proto_type)
    {        
        unsigned NumArgs = func_proto_type->getNumArgs();
        unsigned ArgIndex;
        
        SmallVector<ParmVarDecl *, 5> parm_var_decls;
                
        for (ArgIndex = 0; ArgIndex < NumArgs; ++ArgIndex)
        {
            QualType arg_qual_type (func_proto_type->getArgType(ArgIndex));
            
            parm_var_decls.push_back(ParmVarDecl::Create (*m_ast_source.m_ast_context,
                                                          const_cast<DeclContext*>(m_decl_context),
                                                          SourceLocation(),
                                                          SourceLocation(),
                                                          NULL,
                                                          arg_qual_type,
                                                          NULL,
                                                          SC_Static,
                                                          SC_Static,
                                                          NULL));
        }
        
        func_decl->setParams(ArrayRef<ParmVarDecl*>(parm_var_decls));
    }
    else
    {
        lldb::LogSP log(lldb_private::GetLogIfAllCategoriesSet (LIBLLDB_LOG_EXPRESSIONS));

        log->Printf("Function type wasn't a FunctionProtoType");
    }
    
    m_decls.push_back(func_decl);
    
    return func_decl;
}

clang::NamedDecl *
NameSearchContext::AddGenericFunDecl()
{
    FunctionProtoType::ExtProtoInfo proto_info;
    
    proto_info.Variadic = true;
    
    QualType generic_function_type(m_ast_source.m_ast_context->getFunctionType (m_ast_source.m_ast_context->UnknownAnyTy,    // result
                                                                                NULL,                                        // argument types
                                                                                0,                                           // number of arguments
                                                                                proto_info));
    
    return AddFunDecl(generic_function_type.getAsOpaquePtr());
}

clang::NamedDecl *
NameSearchContext::AddTypeDecl(void *type)
{
    if (type)
    {
        QualType qual_type = QualType::getFromOpaquePtr(type);

        if (const TagType *tag_type = qual_type->getAs<TagType>())
        {
            TagDecl *tag_decl = tag_type->getDecl();
            
            m_decls.push_back(tag_decl);
            
            return tag_decl;
        }
        else if (const ObjCObjectType *objc_object_type = qual_type->getAs<ObjCObjectType>())
        {
            ObjCInterfaceDecl *interface_decl = objc_object_type->getInterface();
            
            m_decls.push_back((NamedDecl*)interface_decl);
            
            return (NamedDecl*)interface_decl;
        }
    }
    return NULL;
}

void 
NameSearchContext::AddLookupResult (clang::DeclContextLookupConstResult result)
{
    for (clang::NamedDecl * const *decl_iterator = result.first;
         decl_iterator != result.second;
         ++decl_iterator)
        m_decls.push_back (*decl_iterator);
}

void
NameSearchContext::AddNamedDecl (clang::NamedDecl *decl)
{
    m_decls.push_back (decl);
}
