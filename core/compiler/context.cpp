/***************************************************************************
 * Performs contextual analysis.
 *
 * Copyright (c) 2008-2021, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRAN.TIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include "context.h"
#include "linker.h"
#include "../shared/instrs.h"

/****************************
  * Emits an error
  ****************************/
void ContextAnalyzer::ProcessError(ParseNode* node, const wstring &msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << node->GetFileName() << L':' << node->GetLineNumber() << L": " << msg << endl;
#endif

  const wstring &str_line_num = ToString(node->GetLineNumber());
  errors.insert(pair<int, wstring>(node->GetLineNumber(), node->GetFileName() + L':' + str_line_num + L": " + msg));
}

/****************************
  * Emits an error
  ****************************/
void ContextAnalyzer::ProcessError(const wstring& fn, int ln, const wstring& msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << fn << L':' << ln << L": " << msg << endl;
#endif

  const wstring& str_line_num = ToString(ln);
  errors.insert(pair<int, wstring>(ln, fn + L':' + str_line_num + L": " + msg));
}

/****************************
 * Formats possible alternative
 * methods
 ****************************/
void ContextAnalyzer::ProcessErrorAlternativeMethods(wstring &message)
{
  if(alt_error_method_names.size() > 0) {
    message += L"\n\tPossible alternative(s):\n";
    for(size_t i = 0; i < alt_error_method_names.size(); ++i) {
      message += L"\t\t" + alt_error_method_names[i] + L'\n';
    }
    alt_error_method_names.clear();
  }
}

/****************************
 * Emits an error
 ****************************/
void ContextAnalyzer::ProcessError(const wstring &fn, const wstring &msg)
{
#ifdef _DEBUG
  GetLogger() << L"\tError: " << msg << endl;
#endif

  errors.insert(pair<int, wstring>(1, fn + L":1: " + msg));
}

/****************************
 * Check for errors detected
 * during the contextual
 * analysis process.
 ****************************/
bool ContextAnalyzer::CheckErrors()
{
  // check and process errors
  if(errors.size()) {
    map<int, wstring>::iterator error;
    for(error = errors.begin(); error != errors.end(); ++error) {
      wcerr << error->second << endl;
    }

    // clean up
    delete program;
    program = nullptr;

    return false;
  }

  return true;
}

/****************************
 * Starts the analysis process
 ****************************/
bool ContextAnalyzer::Analyze()
{
#ifdef _DEBUG
  GetLogger() << L"\n--------- Contextual Analysis ---------" << endl;
#endif
  int class_id = 0;

#ifndef _SYSTEM
  // process libraries classes
  linker->Load();
#endif

  // check uses
  const wstring file_name = program->GetFileName();
  vector<wstring> program_uses = program->GetUses();
  for(size_t i = 0; i < program_uses.size(); ++i) {
    const wstring &name = program_uses[i];
    if(!program->HasBundleName(name) && !linker->HasBundleName(name)) {
      ProcessError(file_name, L"Bundle name '" + name + L"' not defined in program or linked libraries");
    }
  }

  // resolve alias types
  vector<Type*>& types = TypeFactory::Instance()->GetTypes();
  for(size_t i = 0; i < types.size(); ++i) {
    Type* type = types[i];
    if(type->GetType() == ALIAS_TYPE) {
      Type* resloved_type = ResolveAlias(type->GetName(), type->GetFileName(), type->GetLineNumber());
      if(resloved_type) {
        type->Set(resloved_type);
      }
    }
  }

  // add methods for default parameters
  vector<ParsedBundle*> bundles = program->GetBundles();
  for(size_t i = 0; i < bundles.size(); ++i) {
    ParsedBundle* bundle = bundles[i];
    vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      Class* klass = classes[j];
      vector<Method*> methods = klass->GetMethods();
      for(size_t k = 0; k < methods.size(); ++k) {
        AddDefaultParameterMethods(bundle, klass, methods[k]);
      }
    }
  }
  // re-encode method signatures; i.e. fully expand class names
  for(size_t i = 0; i < bundles.size(); ++i) {
    // methods
    ParsedBundle* bundle = bundles[i];
    vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      Class* klass = classes[j];
      vector<Method*> methods = klass->GetMethods();
      for(size_t k = 0; k < methods.size(); ++k) {
        Method* method = methods[k];
        if(!method->IsLambda()) {
          method->EncodeSignature(klass, program, linker);
        }
      }
    }

    // aliases
    vector<Alias*> aliases = bundle->GetAliases();
    for(size_t j = 0; j < aliases.size(); ++j) {
      aliases[j]->EncodeSignature(program, linker);
    }
  }

  // associate re-encoded method signatures with methods
  for(size_t i = 0; i < bundles.size(); ++i) {
    bundle = bundles[i];
    vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      Class* klass = classes[j];
      wstring parent_name = klass->GetParentName();
#ifdef _SYSTEM
      if(parent_name.size() == 0 && klass->GetName() != SYSTEM_BASE_NAME) {
#else
      if(parent_name.size() == 0) {
#endif
        parent_name = SYSTEM_BASE_NAME;
        klass->SetParentName(SYSTEM_BASE_NAME);
      }

      if(parent_name.size()) {
        Class* parent = SearchProgramClasses(parent_name);
        if(parent) {
          klass->SetParent(parent);
          parent->AddChild(klass);
        }
        else {
          LibraryClass* lib_parent = linker->SearchClassLibraries(parent_name, program->GetUses(klass->GetFileName()));
          if(lib_parent) {
            klass->SetLibraryParent(lib_parent);
            lib_parent->AddChild(klass);
          }
          else {
            ProcessError(klass, L"Attempting to inherent from an undefined class type");
          }
        }
      }
      // associate methods
      classes[j]->AssociateMethods();
    }
  }

  // process bundles
  bundles = program->GetBundles();
  for(size_t i = 0; i < bundles.size(); ++i) {
    bundle = bundles[i];
    symbol_table = bundle->GetSymbolTableManager();

    // process enums
    vector<Enum*> enums = bundle->GetEnums();
    for(size_t j = 0; j < enums.size(); ++j) {
      AnalyzeEnum(enums[j], 0);
    }
    // process classes
    vector<Class*> classes = bundle->GetClasses();
    for(size_t j = 0; j < classes.size(); ++j) {
      AnalyzeClass(classes[j], class_id++, 0);
    }
    // check for duplicate instance and class level variables
    AnalyzeDuplicateEntries(classes, 0);
    // process class methods
    for(size_t j = 0; j < classes.size(); ++j) {
      AnalyzeMethods(classes[j], 0);
    }
  }

  // check for entry points
  if(!main_found && !is_lib && !is_web) {
    ProcessError(program->GetFileName(), L"The 'Main(args)' function was not defined");
  }

  if(is_web && !web_found) {
    ProcessError(program->GetFileName(), L"The 'Action(args)' function was not defined");
  }

  return CheckErrors();
}

/****************************
 * Analyzes a class
 ****************************/
void ContextAnalyzer::AnalyzeEnum(Enum* eenum, const int depth)
{
#ifdef _DEBUG
  wstring msg = L"[enum: name='" + eenum->GetName() + L"']";
  Debug(msg, eenum->GetLineNumber(), depth);
#endif

  if(!HasProgramLibraryEnum(eenum->GetName())) {
    ProcessError(eenum, L"Undefined enum: '" + ReplaceSubstring(eenum->GetName(), L"#", L"->") + L"'");
  }

  if(linker->SearchClassLibraries(eenum->GetName(), program->GetUses(eenum->GetFileName())) ||
     linker->SearchEnumLibraries(eenum->GetName(), program->GetUses(eenum->GetFileName()))) {
    ProcessError(eenum, L"Enum '" + ReplaceSubstring(eenum->GetName(), L"#", L"->") +
                 L"' defined in program and shared libraries");
  }
}

/****************************
 * Checks for duplicate instance
 * and class level variables
 ****************************/
void ContextAnalyzer::AnalyzeDuplicateEntries(vector<Class*> &classes, const int depth)
{
  for(size_t i = 0; i < classes.size(); ++i) {
    // declarations
    Class* klass = classes[i];
    vector<Statement*> statements = klass->GetStatements();
    for(size_t j = 0; j < statements.size(); ++j) {
      Declaration* declaration = static_cast<Declaration*>(statements[j]);
      SymbolEntry* entry = declaration->GetEntry();
      if(entry) {
        // duplicate parent
        if(DuplicateParentEntries(entry, klass)) {
          size_t offset = entry->GetName().find(L':');
          if(offset != wstring::npos) {
            ++offset;
            const wstring short_name = entry->GetName().substr(offset, entry->GetName().size() - offset);
            ProcessError(declaration, L"Declaration name '" + short_name + L"' defined in a parent class");
          }
          else {
            ProcessError(declaration, L"Internal compiler error: Invalid entry name");
            exit(1);
          }
        }
      }
    }
  }
}

/****************************
 * Expands and validates methods with
 * default parameters
 ****************************/
void ContextAnalyzer::AddDefaultParameterMethods(ParsedBundle* bundle, Class* klass, Method* method)
{
  // declarations
  vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  if(declarations.size() > 0 && declarations[declarations.size() - 1]->GetAssignment()) {
    bool default_params = true;
    for(int i = (int)declarations.size() - 1; i >= 0; --i) {
      if(declarations[i]->GetAssignment()) {
        if(method->IsVirtual()) {
          ProcessError(method, L"Virtual methods and interfaces cannot contain default parameter values");
          return;
        }

        if(!default_params) {
          ProcessError(declarations.front(), L"Only trailing parameters may have default values");
          return;
        }
      }
      else {
        default_params = false;
      }
    }

    GenerateParameterMethods(bundle, klass, method);
  }
}

/****************************
 * Generates alternative methods for
 * method with default parameter values
 ****************************/
void ContextAnalyzer::GenerateParameterMethods(ParsedBundle* bundle, Class* klass, Method* method)
{
  // find initial parameter offset
  vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  size_t inital_param_offset = 0;

  if(!inital_param_offset) {
    for(size_t i = 0; i < declarations.size(); ++i) {
      Declaration* declaration = declarations[i];
      if(declaration->GetAssignment()) {
        if(!inital_param_offset) {
          inital_param_offset = i;
        }
      }
    }
  }

  // build alternative methods
  while(inital_param_offset < declarations.size()) {
    Method* alt_method = TreeFactory::Instance()->MakeMethod(method->GetFileName(), method->GetLineNumber(),
                                                             method->GetName(), method->GetMethodType(),
                                                             method->IsStatic(), method->IsNative());
    alt_method->SetReturn(method->GetReturn());

    DeclarationList* alt_declarations = TreeFactory::Instance()->MakeDeclarationList();
    StatementList* alt_statements = TreeFactory::Instance()->MakeStatementList();

    bundle->GetSymbolTableManager()->NewParseScope();

    if(inital_param_offset) {
      for(size_t i = 0; i < declarations.size(); ++i) {
        Declaration* declaration = declarations[i]->Copy();
        if(i < inital_param_offset) {
          alt_declarations->AddDeclaration(declaration);
          bundle->GetSymbolTableManager()->CurrentParseScope()->AddEntry(declaration->GetEntry());
        }
        else {
          Assignment* assignment = declaration->GetAssignment();
          assignment->GetExpression()->SetEvalType(declaration->GetEntry()->GetType(), true);
          alt_statements->AddStatement(assignment);
        }
      }
      inital_param_offset++;
    }

    // set statements
    alt_method->SetStatements(alt_statements);
    alt_method->SetDeclarations(alt_declarations);
    alt_method->SetOriginal(method);
    bundle->GetSymbolTableManager()->PreviousParseScope(alt_method->GetParsedName());

    // add method
    if(!klass->AddMethod(alt_method)) {
      ProcessError(method, L"Method or function already overloaded '" + method->GetUserName() + L"'");
    }
  }
}

/****************************
 * Analyzes a class
 ****************************/
void ContextAnalyzer::AnalyzeClass(Class* klass, const int id, const int depth)
{
#ifdef _DEBUG
  wstring msg = L"[class: name='" + klass->GetName() + L"'; id=" + ToString(id) +
    L"; virtual=" + ToString(klass->IsVirtual()) + L"]";
  Debug(msg, klass->GetLineNumber(), depth);
#endif

  current_class = klass;
  current_class->SetCalled(true);

  klass->SetSymbolTable(symbol_table->GetSymbolTable(klass->GetName()));
  if(!HasProgramLibraryClass(klass->GetName())) {
    ProcessError(klass, L"Undefined class: '" + klass->GetName() + L"'");
  }

  if(linker->SearchClassLibraries(klass->GetName(), program->GetUses(klass->GetFileName())) ||
     linker->SearchEnumLibraries(klass->GetName(), program->GetUses(klass->GetFileName()))) {
    ProcessError(klass, L"Class '" + klass->GetName() + L"' defined in shared libraries");
  }

  // check generics
  AnalyzeGenerics(klass, depth);

  // check parent class
  CheckParent(klass, depth);

  // check interfaces
  AnalyzeInterfaces(klass, depth);

  // declarations
  vector<Statement*> statements = klass->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    current_method = nullptr;
    AnalyzeDeclaration(static_cast<Declaration*>(statements[i]), current_class, depth + 1);
  }
}

void ContextAnalyzer::CheckParent(Class* klass, const int depth)
{
  Class* parent_klass = klass->GetParent();
  if(parent_klass && (parent_klass->IsInterface() || parent_klass->HasGenerics())) {
    ProcessError(klass, L"Class '" + klass->GetName() + L"' cannot be derived from a generic or interface");
  }
  else {
    LibraryClass* parent_lib_klass = klass->GetLibraryParent();
    if(parent_lib_klass && parent_lib_klass->IsInterface()) {
      ProcessError(klass, L"Classes cannot be derived from interfaces");
    }
  }
}

/****************************
 * Analyzes methods
 ****************************/
void ContextAnalyzer::AnalyzeMethods(Class* klass, const int depth)
{
#ifdef _DEBUG
  wstring msg = L"[class: name='" + klass->GetName() + L"]";
  Debug(msg, klass->GetLineNumber(), depth);
#endif

  current_class = klass;
  current_table = symbol_table->GetSymbolTable(current_class->GetName());

  // methods
  vector<Method*> methods = klass->GetMethods();
  for(size_t i = 0; i < methods.size(); ++i) {
    AnalyzeMethod(methods[i], depth + 1);
  }

  // look for parent virtual methods
  if(current_class->GetParent() && current_class->GetParent()->IsVirtual()) {
    if(!AnalyzeVirtualMethods(current_class, current_class->GetParent(), depth)) {
      ProcessError(current_class, L"Not all virtual methods have been implemented for the class/interface: " +
                   current_class->GetParent()->GetName());
    }
  }
  else if(current_class->GetLibraryParent() && current_class->GetLibraryParent()->IsVirtual()) {
    if(!AnalyzeVirtualMethods(current_class, current_class->GetLibraryParent(), depth)) {
      ProcessError(current_class, L"Not all virtual methods have been implemented for the class/interface: " +
                   current_class->GetLibraryParent()->GetName());
    }
  }

  // collect anonymous classes
  if(klass->GetAnonymousCall()) {
    anonymous_classes.push_back(klass);
  }
}

/*****************************************************************
 * Check for generic classes and backing interfaces
 *****************************************************************/
void ContextAnalyzer::AnalyzeGenerics(Class* klass, const int depth)
{
  const vector<Class*> generic_classes = klass->GetGenericClasses();
  for(size_t i = 0; i < generic_classes.size(); ++i) {
    Class* generic_class = generic_classes[i];
    // check generic class
    const wstring generic_class_name = generic_class->GetName();
    if(HasProgramLibraryClass(generic_class_name)) {
      ProcessError(klass, L"Generic reference '" + generic_class_name + L"' previously defined as a class");
    }
    // check backing interface
    if(generic_class->HasGenericInterface()) {
      Type* generic_inf_type = generic_class->GetGenericInterface();
      Class* klass_generic_inf = nullptr; LibraryClass* lib_klass_generic_inf = nullptr; 
      if(GetProgramLibraryClass(generic_inf_type, klass_generic_inf, lib_klass_generic_inf)) {
        if(klass_generic_inf) {
          generic_inf_type->SetName(klass_generic_inf->GetName());
        }
        else {
          generic_inf_type->SetName(lib_klass_generic_inf->GetName());
        }
      }
      else {
        const wstring generic_inf_name = generic_inf_type->GetName();
        ProcessError(klass, L"Undefined backing generic interface: '" + generic_inf_name + L"'");
      }
    }
  }
}

/****************************
 * Checks for interface
 * implementations
 ****************************/
void ContextAnalyzer::AnalyzeInterfaces(Class* klass, const int depth)
{
  const vector<wstring> interface_names = klass->GetInterfaceNames();
  vector<Class*> interfaces;
  vector<LibraryClass*> lib_interfaces;
  for(size_t i = 0; i < interface_names.size(); ++i) {
    const wstring &interface_name = interface_names[i];
    Class* inf_klass = SearchProgramClasses(interface_name);
    if(inf_klass) {
      if(!inf_klass->IsInterface()) {
        ProcessError(klass, L"Expected an interface type");
        return;
      }

      // ensure interface methods are virtual
      vector<Method*> methods = inf_klass->GetMethods();
      for(size_t i = 0; i < methods.size(); ++i) {
        if(!methods[i]->IsVirtual()) {
          ProcessError(methods[i], L"Interface method must be defined as 'virtual'");
        }
      }
      // ensure implementation
      if(!AnalyzeVirtualMethods(klass, inf_klass, depth)) {
        ProcessError(klass, L"Not all methods have been implemented for the interface: " + inf_klass->GetName());
      }
      else {
        // add interface
        inf_klass->SetCalled(true);
        inf_klass->AddChild(klass);
        interfaces.push_back(inf_klass);
      }
    }
    else {
      LibraryClass* inf_lib_klass = linker->SearchClassLibraries(interface_name, program->GetUses(current_class->GetFileName()));
      if(inf_lib_klass) {
        if(!inf_lib_klass->IsInterface()) {
          ProcessError(klass, L"Expected an interface type");
          return;
        }

        // ensure interface methods are virtual
        map<const wstring, LibraryMethod*> lib_methods = inf_lib_klass->GetMethods();
        map<const wstring, LibraryMethod*>::iterator iter;
        for(iter = lib_methods.begin(); iter != lib_methods.end(); ++iter) {
          LibraryMethod* lib_method = iter->second;
          if(!lib_method->IsVirtual()) {
            ProcessError(klass, L"Interface method must be defined as 'virtual'");
          }
        }
        // ensure implementation
        if(!AnalyzeVirtualMethods(klass, inf_lib_klass, depth)) {
          ProcessError(klass, L"Not all methods have been implemented for the interface: '" +
                       inf_lib_klass->GetName() + L"'");
        }
        else {
          // add interface
          inf_lib_klass->SetCalled(true);
          inf_lib_klass->AddChild(klass);
          lib_interfaces.push_back(inf_lib_klass);
        }
      }
      else {
        ProcessError(klass, L"Undefined interface: '" + interface_name + L"'");
      }
    }
  }
  // save interfaces
  klass->SetInterfaces(interfaces);
  klass->SetLibraryInterfaces(lib_interfaces);
}

/****************************
 * Checks for virtual method
 * implementations
 ****************************/
bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, Class* virtual_class, const int depth)
{
  // get virtual methods
  bool virtual_methods_defined = true;
  vector<Method*> virtual_class_methods = virtual_class->GetMethods();
  for(size_t i = 0; i < virtual_class_methods.size(); ++i) {
    if(virtual_class_methods[i]->IsVirtual()) {
      // validate that methods have been implemented
      Method* virtual_method = virtual_class_methods[i];
      wstring virtual_method_name = virtual_method->GetEncodedName();

      // search for implementation method via signature
      Method* impl_method = nullptr;
      LibraryMethod* lib_impl_method = nullptr;
      const size_t offset = virtual_method_name.find(':');
      if(offset != wstring::npos) {
        wstring encoded_name = impl_class->GetName() + virtual_method_name.substr(offset);
        impl_method = impl_class->GetMethod(encoded_name);
        if(!impl_method && impl_class->GetParent()) {
          Class* parent_class = impl_class->GetParent();
          while(!impl_method && !lib_impl_method && parent_class) {
            encoded_name = parent_class->GetName() + virtual_method_name.substr(offset);
            impl_method = parent_class->GetMethod(encoded_name);
            // update      
            if(!impl_method && parent_class->GetLibraryParent()) {
              LibraryClass* lib_parent_class = parent_class->GetLibraryParent();
              encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
              lib_impl_method = lib_parent_class->GetMethod(encoded_name);
              break;
            }
            parent_class = parent_class->GetParent();
          }
        }
        else if(impl_class->GetLibraryParent()) {
          LibraryClass* lib_parent_class = impl_class->GetLibraryParent();
          encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
          lib_impl_method = lib_parent_class->GetMethod(encoded_name);
        }
      }

      // validate method
      if(impl_method) {
        AnalyzeVirtualMethod(impl_class, impl_method->GetMethodType(), impl_method->GetReturn(),
                             impl_method->IsStatic(), impl_method->IsVirtual(), virtual_method);
      }
      else if(lib_impl_method) {
        AnalyzeVirtualMethod(impl_class, lib_impl_method->GetMethodType(), lib_impl_method->GetReturn(),
                             lib_impl_method->IsStatic(), lib_impl_method->IsVirtual(), virtual_method);
      }
      else {
        // unable to find method via signature
        virtual_methods_defined = false;
      }
    }
  }

  return virtual_methods_defined;
}

/****************************
 * Analyzes virtual method, which
 * are made when compiling shared
 * libraries.
 ****************************/
void ContextAnalyzer::AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return,
                                           bool impl_is_static, bool impl_is_virtual, Method* virtual_method)
{
  // check method types
  if(impl_mthd_type != virtual_method->GetMethodType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetClass()->GetName());
  }
  // check method returns
  Type* virtual_return = virtual_method->GetReturn();
  if(impl_return->GetType() != virtual_return->GetType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetClass()->GetName());
  }
  else if(impl_return->GetType() == CLASS_TYPE &&
          impl_return->GetName() != virtual_return->GetName()) {
    Class* impl_cls = SearchProgramClasses(impl_return->GetName());
    Class* virtual_cls = SearchProgramClasses(virtual_return->GetName());
    if(impl_cls && virtual_cls && impl_cls != virtual_cls) {
      LibraryClass* impl_lib_cls = linker->SearchClassLibraries(impl_return->GetName(),
                                                                program->GetUses(current_class->GetFileName()));
      LibraryClass* virtual_lib_cls = linker->SearchClassLibraries(virtual_return->GetName(),
                                                                   program->GetUses(current_class->GetFileName()));
      if(impl_lib_cls && virtual_lib_cls && impl_lib_cls != virtual_lib_cls) {
        ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                     virtual_method->GetClass()->GetName());
      }
    }
  }
  // check function vs. method
  if(impl_is_static != virtual_method->IsStatic()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetClass()->GetName());
  }
}

/****************************
 * Analyzes virtual method, which
 * are made when compiling shared
 * libraries.
 ****************************/
bool ContextAnalyzer::AnalyzeVirtualMethods(Class* impl_class, LibraryClass* lib_virtual_class, const int depth)
{
  bool virtual_methods_defined = true;

  // virtual methods
  map<const wstring, LibraryMethod*>::iterator iter;
  map<const wstring, LibraryMethod*> lib_virtual_class_methods = lib_virtual_class->GetMethods();
  for(iter = lib_virtual_class_methods.begin(); iter != lib_virtual_class_methods.end(); ++iter) {
    LibraryMethod* virtual_method = iter->second;
    if(virtual_method->IsVirtual()) {
      wstring virtual_method_name = virtual_method->GetName();

      // validate that methods have been implemented
      Method* impl_method = nullptr;
      LibraryMethod* lib_impl_method = nullptr;
      const size_t offset = virtual_method_name.find(':');
      if(offset != wstring::npos) {
        wstring encoded_name = impl_class->GetName() + virtual_method_name.substr(offset);
        impl_method = impl_class->GetMethod(encoded_name);
        if(!impl_method && impl_class->GetParent()) {
          Class* parent_class = impl_class->GetParent();
          while(!impl_method && !lib_impl_method && parent_class) {
            encoded_name = parent_class->GetName() + virtual_method_name.substr(offset);
            impl_method = parent_class->GetMethod(encoded_name);
            // update      
            if(!impl_method && parent_class->GetLibraryParent()) {
              LibraryClass* lib_parent_class = parent_class->GetLibraryParent();
              encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
              lib_impl_method = lib_parent_class->GetMethod(encoded_name);
              break;
            }
            parent_class = parent_class->GetParent();
          }
        }
        else if(impl_class->GetLibraryParent()) {
          LibraryClass* lib_parent_class = impl_class->GetLibraryParent();
          encoded_name = lib_parent_class->GetName() + virtual_method_name.substr(offset);
          lib_impl_method = lib_parent_class->GetMethod(encoded_name);
        }
      }

      // validate method
      if(impl_method) {
        AnalyzeVirtualMethod(impl_class, impl_method->GetMethodType(), impl_method->GetReturn(),
                             impl_method->IsStatic(), impl_method->IsVirtual(), virtual_method);
      }
      else if(lib_impl_method) {
        AnalyzeVirtualMethod(impl_class, lib_impl_method->GetMethodType(), lib_impl_method->GetReturn(),
                             lib_impl_method->IsStatic(), lib_impl_method->IsVirtual(), virtual_method);
      }
      else {
        // unable to find method via signature
        virtual_methods_defined = false;
      }
    }
  }

  return virtual_methods_defined;
}

/****************************
 * Analyzes virtual method, which
 * are made when compiling shared
 * libraries.
 ****************************/
void ContextAnalyzer::AnalyzeVirtualMethod(Class* impl_class, MethodType impl_mthd_type, Type* impl_return,
                                           bool impl_is_static, bool impl_is_virtual, LibraryMethod* virtual_method)
{
  // check method types
  if(impl_mthd_type != virtual_method->GetMethodType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetLibraryClass()->GetName());
  }
  // check method returns
  Type* virtual_return = virtual_method->GetReturn();
  if(impl_return->GetType() != virtual_return->GetType()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetLibraryClass()->GetName());
  }
  else if(impl_return->GetType() == CLASS_TYPE &&
          impl_return->GetName() != virtual_return->GetName()) {
    Class* impl_cls = SearchProgramClasses(impl_return->GetName());
    Class* virtual_cls = SearchProgramClasses(virtual_return->GetName());
    if(impl_cls && virtual_cls && impl_cls != virtual_cls) {
      LibraryClass* impl_lib_cls = linker->SearchClassLibraries(impl_return->GetName(),
                                                                program->GetUses(current_class->GetFileName()));
      LibraryClass* virtual_lib_cls = linker->SearchClassLibraries(virtual_return->GetName(),
                                                                   program->GetUses(current_class->GetFileName()));
      if(impl_lib_cls && virtual_lib_cls && impl_lib_cls != virtual_lib_cls) {
        ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                     virtual_method->GetLibraryClass()->GetName());
      }
    }
  }
  // check function vs. method
  if(impl_is_static != virtual_method->IsStatic()) {
    ProcessError(impl_class, L"Not all virtual methods have been defined for class/interface: " +
                 virtual_method->GetLibraryClass()->GetName());
  }
  // check virtual
  if(impl_is_virtual) {
    ProcessError(impl_class, L"Implementation method cannot be virtual");
  }
}

/****************************
 * Analyzes a method
 ****************************/
void ContextAnalyzer::AnalyzeMethod(Method* method, const int depth)
{
#ifdef _DEBUG
  wstring msg = L"(method: name='" + method->GetName() + L"; parsed='" + method->GetParsedName() + L"')"; 
  Debug(msg, method->GetLineNumber(), depth);
#endif

  method->SetId();
  current_method = method;
  current_table = symbol_table->GetSymbolTable(method->GetParsedName());
  method->SetSymbolTable(current_table);

  // declarations
  vector<Declaration*> declarations = method->GetDeclarations()->GetDeclarations();
  for(size_t i = 0; i < declarations.size(); ++i) {
    AnalyzeDeclaration(declarations[i], current_class, depth + 1);
  }

  // process statements if function/method is not virtual
  if(!method->IsVirtual()) {
    // statements
    vector<Statement*> statements = method->GetStatements()->GetStatements();
    for(size_t i = 0; i < statements.size(); ++i) {
      AnalyzeStatement(statements[i], depth + 1);
    }

    // check for parent call
    if((method->GetMethodType() == NEW_PUBLIC_METHOD ||
       method->GetMethodType() == NEW_PRIVATE_METHOD) &&
       (current_class->GetParent() || (current_class->GetLibraryParent() &&
       current_class->GetLibraryParent()->GetName() != SYSTEM_BASE_NAME))) {
      if(statements.size() == 0 || statements.front()->GetStatementType() != METHOD_CALL_STMT) {
        if(!current_class->IsInterface()) {
          ProcessError(method, L"Parent call required");
        }
      }
      else {
        MethodCall* mthd_call = static_cast<MethodCall*>(statements.front());
        if(mthd_call->GetCallType() != PARENT_CALL && !current_class->IsInterface()) {
          ProcessError(method, L"Parent call required");
        }
      }
    }

#ifndef _SYSTEM
    // check for return
    if(method->GetMethodType() != NEW_PUBLIC_METHOD &&
       method->GetMethodType() != NEW_PRIVATE_METHOD &&
       method->GetReturn()->GetType() != NIL_TYPE) {
      if(!AnalyzeReturnPaths(method->GetStatements(), depth + 1) && !method->IsAlt()) {
        ProcessError(method, L"All method/function paths must return a value");
      }
    }
#endif

    // check program main
    const wstring main_str = current_class->GetName() + L":Main:o.System.String*,";
    if(method->GetEncodedName() == main_str) {
      if(main_found) {
        ProcessError(method, L"The 'Main(args)' function has already been defined");
      }
      else if(method->IsStatic()) {
        current_class->SetCalled(true);
        program->SetStart(current_class, method);
        main_found = true;
      }

      if(main_found && (is_lib || is_web)) {
        ProcessError(method, L"Libraries and web applications may not define a 'Main(args)' function");
      }
    }
    // web program
    else if(is_web) {
      const wstring web_str = current_class->GetName() + L":Action:o.Web.FastCgi.Request,o.Web.FastCgi.Response,";
      if(method->GetEncodedName() == web_str) {
        if(web_found) {
          ProcessError(method, L"The 'Action(args)' function has already been defined");
        }
        else if(method->IsStatic()) {
          current_class->SetCalled(true);
          program->SetStart(current_class, method);
          web_found = true;
        }

        if(web_found && (is_lib || main_found)) {
          ProcessError(method, L"Web applications may not define a 'Main(args)' function or be compiled as a library");
        }
      }
    }
  }
}

/****************************
 * Analyzes a lambda function
 ****************************/
void ContextAnalyzer::AnalyzeLambda(Lambda* lambda, const int depth)
{
  // already been checked
  if(lambda->GetMethodCall()) {
    return;
  }

  // by type
  Type* lambda_type = nullptr;
  const wstring lambda_name = lambda->GetName();
  bool is_inferred = HasInferredLambdaTypes(lambda_name);

  if(lambda->GetLambdaType()) {
    lambda_type = lambda->GetLambdaType();
  }
  // by name
  else if(!is_inferred) {
    lambda_type = ResolveAlias(lambda_name, lambda);
  }

  if(lambda_type) {
    BuildLambdaFunction(lambda, lambda_type, depth);
  }
  // derived type
  else if(is_inferred) {
    lambda_inferred.first = lambda;
  }
  else {
    ProcessError(lambda, L"Invalid lambda type");
  }
}

Type* ContextAnalyzer::ResolveAlias(const wstring& name, const wstring& fn, int ln) {
  Type* alias_type = nullptr;

  wstring alias_name;
  const size_t middle = name.find(L'#');
  if(middle != wstring::npos) {
    alias_name = name.substr(0, middle);
  }

  wstring type_name;
  if(middle + 1 < name.size()) {
    type_name = name.substr(middle + 1);
  }

  Alias* alias = program->GetAlias(alias_name);
  if(alias) {
    alias_type = alias->GetType(type_name);
    if(alias_type) {
      alias_type = TypeFactory::Instance()->MakeType(alias_type);
    }
    else {
      if(name.empty()) {
        ProcessError(fn, ln, L"Invalid alias");
      }
      else {
        ProcessError(fn, ln, L"Undefined alias: '" + ReplaceSubstring(name, L"#", L"->") + L"'");
      }
    }
  }
  else {
    LibraryAlias* lib_alias = linker->SearchAliasLibraries(alias_name, program->GetUses(fn));
    if(lib_alias) {
      alias_type = lib_alias->GetType(type_name);
      if(alias_type) {
        alias_type = TypeFactory::Instance()->MakeType(alias_type);
      }
      else {
        if(name.empty()) {
          ProcessError(fn, ln, L"Invalid alias");
        }
        else {
          ProcessError(fn, ln, L"Undefined alias: '" + ReplaceSubstring(name, L"#", L"->") + L"'");
        }
      }
    }
    else {
      if(name.empty()) {
        ProcessError(fn, ln, L"Invalid alias");
      }
      else {
        ProcessError(fn, ln, L"Undefined alias: '" + ReplaceSubstring(name, L"#", L"->") + L"'");
      }
    }
  }  

  if(alias_type && alias_type->GetType() == ALIAS_TYPE) {
    ProcessError(fn, ln, L"Invalid nested alias reference");
    return nullptr;
  }
  
  return alias_type;
}

Method* ContextAnalyzer::DerivedLambdaFunction(vector<Method*>& alt_mthds)
{
  if(lambda_inferred.first && lambda_inferred.second && alt_mthds.size() == 1) {
    MethodCall* lambda_inferred_call = lambda_inferred.second;
    Method* alt_mthd = alt_mthds.front();
    vector<Declaration*> alt_mthd_types = alt_mthd->GetDeclarations()->GetDeclarations();
    if(alt_mthd_types.size() == 1 && alt_mthd_types.front()->GetEntry() && 
       alt_mthd_types.front()->GetEntry()->GetType()->GetType() == FUNC_TYPE) {
      // set parameters
      vector<Type*> inferred_type_params;
      Type* alt_mthd_type = alt_mthd_types.front()->GetEntry()->GetType();
      const vector<Type*> func_params = alt_mthd_type->GetFunctionParameters();
      for(size_t i = 0; i < func_params.size(); ++i) {
        inferred_type_params.push_back(ResolveGenericType(func_params[i], lambda_inferred_call, alt_mthd->GetClass(), nullptr));
      }
      // set return
      Type* inferred_type_rtrn = ResolveGenericType(alt_mthd_type->GetFunctionReturn(), lambda_inferred_call, alt_mthd->GetClass(), nullptr);

      Type* inferred_type = TypeFactory::Instance()->MakeType(FUNC_TYPE);
      inferred_type->SetFunctionParameters(inferred_type_params);
      inferred_type->SetFunctionReturn(inferred_type_rtrn);

      // build lambda function
      BuildLambdaFunction(lambda_inferred.first, inferred_type, 0);
      return alt_mthd;
    }
  }

  return nullptr;
}

LibraryMethod* ContextAnalyzer::DerivedLambdaFunction(vector<LibraryMethod*>& alt_mthds)
{
  if(lambda_inferred.first && lambda_inferred.second && alt_mthds.size() == 1) {
    MethodCall* lambda_inferred_call = lambda_inferred.second;
    LibraryMethod* alt_mthd = alt_mthds.front();
    vector<frontend::Type*> alt_mthd_types = alt_mthd->GetDeclarationTypes();
    if(alt_mthd_types.size() == 1 && alt_mthd_types.front()->GetType() == FUNC_TYPE) {
      // set parameters
      vector<Type*> inferred_type_params;
      Type* alt_mthd_type = alt_mthd_types.front();
      const vector<Type*> func_params = alt_mthd_type->GetFunctionParameters();
      for(size_t i = 0; i < func_params.size(); ++i) {
        inferred_type_params.push_back(ResolveGenericType(func_params[i], lambda_inferred_call, NULL, alt_mthd->GetLibraryClass()));
      }
      // set return
      Type* inferred_type_rtrn = ResolveGenericType(alt_mthd_type->GetFunctionReturn(), lambda_inferred_call, NULL, alt_mthd->GetLibraryClass());
      
      Type* inferred_type = TypeFactory::Instance()->MakeType(FUNC_TYPE);
      inferred_type->SetFunctionParameters(inferred_type_params);
      inferred_type->SetFunctionReturn(inferred_type_rtrn);

      // build lambda function
      BuildLambdaFunction(lambda_inferred.first, inferred_type, 0);
      return alt_mthd;
    }
  }

  return nullptr;
}

void ContextAnalyzer::BuildLambdaFunction(Lambda* lambda, Type* lambda_type, const int depth)
{
  // set return
  Method* method = lambda->GetMethod();
  current_method->SetAndOr(true);
  method->SetReturn(lambda_type->GetFunctionReturn());

  // update declarations
  vector<Type*> types = lambda_type->GetFunctionParameters();
  DeclarationList* declaration_list = method->GetDeclarations();
  vector<Declaration*> declarations = declaration_list->GetDeclarations();
  if(types.size() == declarations.size()) {
    // encode lookup
    method->EncodeSignature();

    for(size_t i = 0; i < declarations.size(); ++i) {
      declarations[i]->GetEntry()->SetType(types[i]);
    }

    current_class->AddMethod(method);
    method->EncodeSignature(current_class, program, linker);
    current_class->AssociateMethod(method);

    // check method and restore context
    capture_lambda = lambda;
    capture_method = current_method;
    capture_table = current_table;

    AnalyzeMethod(method, depth + 1);

    current_table = capture_table;
    capture_table = nullptr;

    current_method = capture_method;
    capture_method = nullptr;
    capture_lambda = nullptr;

    const wstring full_method_name = method->GetName();
    const size_t offset = full_method_name.find(':');
    if(offset != wstring::npos) {
      const wstring method_name = full_method_name.substr(offset + 1);

      // create method call
      MethodCall* method_call = TreeFactory::Instance()->MakeMethodCall(method->GetFileName(), method->GetLineNumber(),
                                                                        current_class->GetName(), method_name,
                                                                        MapLambdaDeclarations(declaration_list));
      method_call->SetFunctionalReturn(method->GetReturn());
      AnalyzeMethodCall(method_call, depth + 1);
      lambda->SetMethodCall(method_call);
      lambda->SetTypes(method_call->GetEvalType());
    }
    else {
      wcerr << L"Internal compiler error: Invalid method name." << endl;
      exit(1);
    }
  }
  else {
    ProcessError(lambda, L"Deceleration and parameter size mismatch");
  }
}

/****************************
 * maps lambda decelerations 
 * to parameter list
 ****************************/
ExpressionList* ContextAnalyzer::MapLambdaDeclarations(DeclarationList* declarations)
{
  ExpressionList* expressions = TreeFactory::Instance()->MakeExpressionList();

  const vector<Declaration*> dclrs = declarations->GetDeclarations();
  for(size_t i = 0; i < dclrs.size(); ++i) {
    wstring ident;
    Type* dclr_type = dclrs[i]->GetEntry()->GetType();
    switch(dclr_type->GetType()) {
    case NIL_TYPE:
    case VAR_TYPE:
      break;

    case BOOLEAN_TYPE:
      ident = BOOL_CLASS_ID;
      break;

    case BYTE_TYPE:
      ident = BYTE_CLASS_ID;
      break;

    case CHAR_TYPE:
      ident = CHAR_CLASS_ID;
      break;

    case INT_TYPE:
      ident = INT_CLASS_ID;
      break;

    case  FLOAT_TYPE:
      ident = FLOAT_CLASS_ID;
      break;

    case CLASS_TYPE:
    case FUNC_TYPE:
      ident = dclr_type->GetName();
      break;
        
    case ALIAS_TYPE:
      break;
    }

    if(!ident.empty()) {
      expressions->AddExpression(TreeFactory::Instance()->MakeVariable(dclrs[i]->GetFileName(), dclrs[i]->GetLineNumber(), ident));
    }
  }

  return expressions;
}

/****************************
 * Check to determine if lambda 
 * concrete types are inferred
 ****************************/
bool ContextAnalyzer::HasInferredLambdaTypes(const wstring lambda_name)
{
  return lambda_inferred.second && lambda_name.empty();
}

void ContextAnalyzer::CheckLambdaInferredTypes(MethodCall* method_call, int depth)
{
  ExpressionList* call_params = method_call->GetCallingParameters();
  if(call_params->GetExpressions().size() == 1 && call_params->GetExpressions().at(0)->GetExpressionType() == LAMBDA_EXPR) {
    lambda_inferred.second = method_call;
  }
  else {
    lambda_inferred.first = nullptr;
    lambda_inferred.second = nullptr;
  }
}

/****************************
 * Analyzes method return
 * paths
 ****************************/
bool ContextAnalyzer::AnalyzeReturnPaths(StatementList* statement_list, const int depth)
{
  vector<Statement*> statements = statement_list->GetStatements();
  if(statements.size() == 0) {
    ProcessError(current_method, L"All method/function paths must return a value");
  }
  else {
    Statement* last_statement = statements.back();
    switch(last_statement->GetStatementType()) {
    case SELECT_STMT:
      return AnalyzeReturnPaths(static_cast<Select*>(last_statement), depth + 1);

    case IF_STMT:
      return AnalyzeReturnPaths(static_cast<If*>(last_statement), false, depth + 1);

    case RETURN_STMT:
      return true;

    default:
      if(!current_method->IsAlt()) {
        ProcessError(current_method, L"All method/function paths must return a value");
      }
      break;
    }
  }

  return false;
}

bool ContextAnalyzer::AnalyzeReturnPaths(If* if_stmt, bool nested, const int depth)
{
  bool if_ok = false;
  bool if_else_ok = false;
  bool else_ok = false;

  // 'if' statements
  StatementList* if_list = if_stmt->GetIfStatements();
  if(if_list) {
    if_ok = AnalyzeReturnPaths(if_list, depth + 1);
  }

  If* next = if_stmt->GetNext();
  if(next) {
    if_else_ok = AnalyzeReturnPaths(next, true, depth);
  }

  // 'else'
  StatementList* else_list = if_stmt->GetElseStatements();
  if(else_list) {
    else_ok = AnalyzeReturnPaths(else_list, depth + 1);
  }
  else if(!if_else_ok) {
    return false;
  }

  // if and else
  if(!next) {
    return if_ok && (else_ok || if_else_ok);
  }

  // if, else-if and else
  if(if_ok && if_else_ok) {
    return true;
  }

  return false;
}

bool ContextAnalyzer::AnalyzeReturnPaths(Select* select_stmt, const int depth)
{
  map<ExpressionList*, StatementList*> statements = select_stmt->GetStatements();
  map<int, StatementList*> label_statements;
  for(map<ExpressionList*, StatementList*>::iterator iter = statements.begin(); iter != statements.end(); ++iter) {
    if(!AnalyzeReturnPaths(iter->second, depth + 1)) {
      return false;
    }
  }

  StatementList* other_stmts = select_stmt->GetOther();
  if(other_stmts) {
    if(!AnalyzeReturnPaths(other_stmts, depth + 1)) {
      return false;
    }
  }
  else {
    return false;
  }

  return true;
}

/****************************
 * Analyzes a statements
 ****************************/
void ContextAnalyzer::AnalyzeStatements(StatementList* statement_list, const int depth)
{
  current_table->NewScope();
  vector<Statement*> statements = statement_list->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    AnalyzeStatement(statements[i], depth + 1);
  }
  current_table->PreviousScope();
}

/****************************
 * Analyzes a statement
 ****************************/
void ContextAnalyzer::AnalyzeStatement(Statement* statement, const int depth)
{
  switch(statement->GetStatementType()) {
  case EMPTY_STMT:
  case SYSTEM_STMT:
    break;

  case DECLARATION_STMT: {
    Declaration* declaration = static_cast<Declaration*>(statement);
    if(declaration->GetChild()) {
      // build stack declarations
      stack<Declaration*> declarations;
      while(declaration) {
        declarations.push(declaration);
        declaration = declaration->GetChild();
      }
      // process declarations
      while(!declarations.empty()) {
        AnalyzeDeclaration(declarations.top(), current_class, depth);
        declarations.pop();
      }
    }
    else {
      AnalyzeDeclaration(static_cast<Declaration*>(statement), current_class, depth);
    }
  }
    break;

  case METHOD_CALL_STMT: {
    MethodCall* mthd_call = static_cast<MethodCall*>(statement);
    AnalyzeMethodCall(mthd_call, depth);
    AnalyzeCast(mthd_call, depth + 1);
  }
    break;


  case ADD_ASSIGN_STMT:
    AnalyzeAssignment(static_cast<Assignment*>(statement), statement->GetStatementType(), depth);
    break;

  case SUB_ASSIGN_STMT:
  case MUL_ASSIGN_STMT:
  case DIV_ASSIGN_STMT:
    AnalyzeAssignment(static_cast<Assignment*>(statement), statement->GetStatementType(), depth);
    break;

  case ASSIGN_STMT: {
    Assignment* assignment = static_cast<Assignment*>(statement);
    if(assignment->GetChild()) {
      // build stack assignments
      stack<Assignment*> assignments;
      while(assignment) {
        assignments.push(assignment);
        assignment = assignment->GetChild();
      }
      // process assignments
      while(!assignments.empty()) {
        AnalyzeAssignment(assignments.top(), statement->GetStatementType(), depth);
        assignments.pop();
      }
    }
    else {
      AnalyzeAssignment(assignment, statement->GetStatementType(), depth);
    }
  }
    break;

  case SIMPLE_STMT:
    AnalyzeSimpleStatement(static_cast<SimpleStatement*>(statement), depth);
    break;

  case RETURN_STMT:
    AnalyzeReturn(static_cast<Return*>(statement), depth);
    break;

  case LEAVING_STMT:
    AnalyzeLeaving(static_cast<Leaving*>(statement), depth);
    break;

  case IF_STMT:
    AnalyzeIf(static_cast<If*>(statement), depth);
    break;

  case DO_WHILE_STMT:
    AnalyzeDoWhile(static_cast<DoWhile*>(statement), depth);
    break;

  case WHILE_STMT:
    AnalyzeWhile(static_cast<While*>(statement), depth);
    break;

  case FOR_STMT:
    AnalyzeFor(static_cast<For*>(statement), depth);
    break;

  case BREAK_STMT:
  case CONTINUE_STMT:
    if(in_loop <= 0) {
      ProcessError(statement, L"Breaks are only allowed in loops.");
    }
    break;

  case SELECT_STMT:
    current_method->SetAndOr(true);
    AnalyzeSelect(static_cast<Select*>(statement), depth);
    break;

  case CRITICAL_STMT:
    AnalyzeCritical(static_cast<CriticalSection*>(statement), depth);
    break;

  default:
    ProcessError(statement, L"Undefined statement");
    break;
  }
}

/****************************
 * Analyzes an expression
 ****************************/
void ContextAnalyzer::AnalyzeExpression(Expression* expression, const int depth)
{
  switch(expression->GetExpressionType()) {
  case LAMBDA_EXPR:
    AnalyzeLambda(static_cast<Lambda*>(expression), depth);
    break;
  
  case STAT_ARY_EXPR:
    AnalyzeStaticArray(static_cast<StaticArray*>(expression), depth);
    break;

  case CHAR_STR_EXPR:
    AnalyzeCharacterString(static_cast<CharacterString*>(expression), depth + 1);
    break;

  case COND_EXPR:
    AnalyzeConditional(static_cast<Cond*>(expression), depth);
    break;

  case METHOD_CALL_EXPR:
    AnalyzeMethodCall(static_cast<MethodCall*>(expression), depth);
    break;

  case NIL_LIT_EXPR:
#ifdef _DEBUG
    Debug(L"nil literal", expression->GetLineNumber(), depth);
#endif
    break;

  case BOOLEAN_LIT_EXPR:
#ifdef _DEBUG
    Debug(L"boolean literal", expression->GetLineNumber(), depth);
#endif
    break;

  case CHAR_LIT_EXPR:
#ifdef _DEBUG
    Debug(L"character literal", expression->GetLineNumber(), depth);
#endif
    break;

  case INT_LIT_EXPR:
#ifdef _DEBUG
    Debug(L"integer literal", expression->GetLineNumber(), depth);
#endif
    break;

  case FLOAT_LIT_EXPR:
#ifdef _DEBUG
    Debug(L"float literal", expression->GetLineNumber(), depth);
#endif
    break;

  case VAR_EXPR:
    AnalyzeVariable(static_cast<Variable*>(expression), depth);
    break;

  case AND_EXPR:
  case OR_EXPR:
    current_method->SetAndOr(true);
    AnalyzeCalculation(static_cast<CalculatedExpression*>(expression), depth + 1);
    break;

  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EXPR:
  case LES_EQL_EXPR:
  case GTR_EQL_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
  case SHL_EXPR:
  case SHR_EXPR:
  case BIT_AND_EXPR:
  case BIT_OR_EXPR:
  case BIT_XOR_EXPR:
    AnalyzeCalculation(static_cast<CalculatedExpression*>(expression), depth + 1);
    break;

  default:
    ProcessError(expression, L"Undefined expression");
    break;
  }

  // check expression method call
  AnalyzeExpressionMethodCall(expression, depth + 1);

  // check cast
  AnalyzeCast(expression, depth + 1);
}

/****************************
 * Analyzes a ternary
 * conditional
 ****************************/
void ContextAnalyzer::AnalyzeConditional(Cond* conditional, const int depth)
{
#ifdef _DEBUG
  Debug(L"conditional expression", conditional->GetLineNumber(), depth);
#endif

  // check expressions
  AnalyzeExpression(conditional->GetCondExpression(), depth + 1);
  Expression* if_conditional = conditional->GetExpression();
  AnalyzeExpression(if_conditional, depth + 1);
  Expression* else_conditional = conditional->GetElseExpression();
  AnalyzeExpression(else_conditional, depth + 1);

  Type* if_type = GetExpressionType(if_conditional, depth + 1);
  Type* else_type = GetExpressionType(else_conditional, depth + 1);

  // validate types
  if(if_type) {
    if(if_type->GetType() == CLASS_TYPE && else_type->GetType() == CLASS_TYPE) {
      AnalyzeClassCast(if_conditional->GetEvalType(), else_conditional, depth + 1);
    }
    else if(else_type && (if_type->GetType() != else_type->GetType() &&
            !((if_type->GetType() == CLASS_TYPE && else_type->GetType() == NIL_TYPE) ||
            (if_type->GetType() == NIL_TYPE && else_type->GetType() == CLASS_TYPE)))) {
      ProcessError(conditional, L"'?' invalid type mismatch");
    }
    // set eval type
    conditional->SetEvalType(if_conditional->GetEvalType(), true);
    current_method->SetAndOr(true);
  }
  else {
    ProcessError(conditional, L"Invalid 'if' statement");
  }
}

/****************************
 * Analyzes a character literal
 ****************************/
void ContextAnalyzer::AnalyzeCharacterString(CharacterString* char_str, const int depth)
{
#ifdef _DEBUG
  Debug(L"character string literal", char_str->GetLineNumber(), depth);
#endif

  int var_start = -1;
  int str_start = 0;
  const wstring &str = char_str->GetString();

  // empty wstring segment
  if(str.empty()) {
    char_str->AddSegment(L"");
  }
  else {
    // process segment
    for(size_t i = 0; i < str.size(); ++i) {
      // variable start
      if(str[i] == L'{' && i + 1 < str.size() && str[i + 1] == L'$') {
        var_start = (int)i;
        const wstring token = str.substr(str_start, i - str_start);
        char_str->AddSegment(token);
      }

      // variable end
      if(var_start > -1) {
        if(str[i] == L'}') {
          const wstring token = str.substr(var_start + 2, i - var_start - 2);
          SymbolEntry* entry = GetEntry(token);
          if(entry) {
            AnalyzeCharacterStringVariable(entry, char_str, depth);
          }
          else {
            ProcessError(char_str, L"Undefined variable: '" + token + L"'");
          }
          // update
          var_start = -1;
          str_start = (int)i + 1;
        }
        else if(i + 1 == str.size()) {
          const wstring token = str.substr(var_start + 1, i - var_start);
          SymbolEntry* entry = GetEntry(token);
          if(entry) {
            AnalyzeCharacterStringVariable(entry, char_str, depth);
          }
          else {
            ProcessError(char_str, L"Undefined variable: '" + token + L"'");
          }
          // update
          var_start = -1;
          str_start = (int)i + 1;
        }
      }
      else if(i + 1 == str.size()) {
        var_start = (int)i;
        const wstring token = str.substr(str_start, i - str_start + 1);
        char_str->AddSegment(token);
      }
    }
  }

  // tag literal strings
  vector<CharacterStringSegment*> segments = char_str->GetSegments();
  for(size_t i = 0; i < segments.size(); ++i) {
    if(segments[i]->GetType() == STRING) {
      int id = program->GetCharStringId(segments[i]->GetString());
      if(id > -1) {
        segments[i]->SetId(id);
      }
      else {
        segments[i]->SetId(char_str_index);
        program->AddCharString(segments[i]->GetString(), char_str_index);
        char_str_index++;
      }
    }
  }

  // create temporary variable for concat of strings and variables
  if(segments.size() > 1) {
    Type* type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.String");
    const wstring scope_name = current_method->GetName() + L":#concat#";
    SymbolEntry* entry = current_table->GetEntry(scope_name);
    if(!entry) {
      entry = TreeFactory::Instance()->MakeSymbolEntry(char_str->GetFileName(),
                                                       char_str->GetLineNumber(),
                                                       scope_name, type, false, true);
      current_table->AddEntry(entry, true);
    }
    char_str->SetConcat(entry);
  }

#ifndef _SYSTEM
  LibraryClass* lib_klass = linker->SearchClassLibraries(L"System.String", program->GetUses(current_class->GetFileName()));
  if(lib_klass) {
    lib_klass->SetCalled(true);
  }
  else {
    ProcessError(char_str, L"Internal compiler error: Invalid class name");
    exit(1);
  }
#endif

  char_str->SetProcessed();
}

/****************************
 * Analyzes a static array
 ****************************/
void ContextAnalyzer::AnalyzeStaticArray(StaticArray* array, const int depth)
{
  // TOOD: support for 3d or 4d initialization
  if(array->GetDimension() > 2) {
    ProcessError(array, L"Invalid static array declaration.");
    return;
  }

  if(!array->IsMatchingTypes()) {
    ProcessError(array, L"Array element types do not match.");
    return;
  }

  if(!array->IsMatchingLenghts()) {
    ProcessError(array, L"Array dimension lengths do not match.");
    return;
  }

  Type* type = TypeFactory::Instance()->MakeType(array->GetType());
  type->SetDimension(array->GetDimension());
  if(type->GetType() == CLASS_TYPE) {
    type->SetName(L"System.String");
  }
  array->SetEvalType(type, false);

  // ensure that element sizes match dimensions
  vector<Expression*> all_elements = array->GetAllElements()->GetExpressions();
  switch(array->GetType()) {
  case INT_TYPE: {
    int id = program->GetIntStringId(all_elements);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(int_str_index);
      program->AddIntString(all_elements, int_str_index);
      int_str_index++;
    }
  }
    break;

  case FLOAT_TYPE: {
    int id = program->GetFloatStringId(all_elements);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(float_str_index);
      program->AddFloatString(all_elements, float_str_index);
      float_str_index++;
    }
  }
    break;

  case CHAR_TYPE: {
    // copy wstring elements
    wstring char_str;
    for(size_t i = 0; i < all_elements.size(); ++i) {
      char_str += static_cast<CharacterLiteral*>(all_elements[i])->GetValue();
    }
    // associate char wstring
    int id = program->GetCharStringId(char_str);
    if(id > -1) {
      array->SetId(id);
    }
    else {
      array->SetId(char_str_index);
      program->AddCharString(char_str, char_str_index);
      char_str_index++;
    }
  }
    break;

  case CLASS_TYPE:
    for(size_t i = 0; i < all_elements.size(); ++i) {
      AnalyzeCharacterString(static_cast<CharacterString*>(all_elements[i]), depth + 1);
    }
    break;

  default:
    ProcessError(array, L"Invalid type for static array.");
    break;
  }
}

/****************************
 * Analyzes a variable
 ****************************/
void ContextAnalyzer::AnalyzeVariable(Variable* variable, const int depth)
{
  AnalyzeVariable(variable, GetEntry(variable->GetName()), depth);
}

void ContextAnalyzer::AnalyzeVariable(Variable* variable, SymbolEntry* entry, const int depth)
{
  // explicitly defined variable
  if(entry) {
#ifdef _DEBUG
    wstring msg = L"variable reference: name='" + variable->GetName() + L"' local=" +
      (entry->IsLocal() ? L"true" : L"false");
    Debug(msg, variable->GetLineNumber(), depth);
#endif

    const wstring &name = variable->GetName();
    if(HasProgramLibraryEnum(name) || HasProgramLibraryClass(name)) {
      ProcessError(variable, L"Variable '" + name + L"' already used to define a class, enum or function\n\tIf passing a function reference ensure the full signature is provided");
    }

    // associate variable and entry
    if(!variable->GetEvalType()) {
      Type* entry_type = entry->GetType();
      Expression* expression = variable;

      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        expression = expression->GetMethodCall();
      }

      Type* cast_type = expression->GetCastType();
      if(cast_type && cast_type->GetType() == CLASS_TYPE && entry_type && entry_type->GetType() == CLASS_TYPE && !HasProgramLibraryEnum(entry_type->GetName())) {
        AnalyzeClassCast(expression->GetCastType(), entry_type, expression, false, depth + 1);
      }

      variable->SetTypes(entry_type);
      variable->SetEntry(entry);
      entry->AddVariable(variable);
    }

    // array parameters
    ExpressionList* indices = variable->GetIndices();
    if(indices) {
      // check dimensions
      if(entry->GetType() && entry->GetType()->GetDimension() == (int)indices->GetExpressions().size()) {
        AnalyzeIndices(indices, depth + 1);
      }
      else {
        ProcessError(variable, L"Dimension size mismatch or uninitialized type");
      }
    }

    // static check
    if(InvalidStatic(entry)) {
      ProcessError(variable, L"Cannot reference an instance variable from this context");
    }
  }
  // lambda expressions
  else if(current_method && current_method->IsLambda()) {
    const wstring capture_scope_name = capture_method->GetName() + L':' + variable->GetName();
    SymbolEntry* capture_entry = capture_table->GetEntry(capture_scope_name);
    if(capture_entry) {
      if(capture_lambda->HasClosure(capture_entry)) {
        SymbolEntry* copy_entry = capture_lambda->GetClosure(capture_entry);
        variable->SetTypes(copy_entry->GetType());
        variable->SetEntry(copy_entry);
        copy_entry->AddVariable(variable);
      }
      else {
        const wstring var_scope_name = current_method->GetName() + L':' + variable->GetName();
        SymbolEntry* copy_entry = TreeFactory::Instance()->MakeSymbolEntry(variable->GetFileName(), variable->GetLineNumber(),
                                                                           var_scope_name, capture_entry->GetType(), false, false);
        symbol_table->GetSymbolTable(current_class->GetName())->AddEntry(copy_entry, true);

        variable->SetTypes(copy_entry->GetType());
        variable->SetEntry(copy_entry);
        copy_entry->AddVariable(variable);
        capture_lambda->AddClosure(copy_entry, capture_entry);
      }
    }
  }
  // type inferred variable
  else if(current_method) {
    const wstring scope_name = current_method->GetName() + L':' + variable->GetName();
    SymbolEntry* var_entry = TreeFactory::Instance()->MakeSymbolEntry(variable->GetFileName(), variable->GetLineNumber(), scope_name, 
                                                                      TypeFactory::Instance()->MakeType(VAR_TYPE), false, true);
    current_table->AddEntry(var_entry, true);

    // link entry and variable
    variable->SetTypes(var_entry->GetType());
    variable->SetEntry(var_entry);
    var_entry->AddVariable(variable);
  }
  // undefined variable (at class level)
  else {
    ProcessError(variable, L"Undefined variable: '" + variable->GetName() + L"'");
  }

  if(variable->GetPreStatement() && variable->GetPostStatement()) {
    ProcessError(variable, L"Variable cannot have pre and pos operations");
  }
  else if(variable->GetPreStatement() && !variable->IsPreStatementChecked()) {
    OperationAssignment* pre_stmt = variable->GetPreStatement();
    variable->PreStatementChecked();
    AnalyzeAssignment(pre_stmt, pre_stmt->GetStatementType(), depth + 1);
  }
  else if(variable->GetPostStatement() && !variable->IsPostStatementChecked()) {
    OperationAssignment* post_stmt = variable->GetPostStatement();
    variable->PostStatementChecked();
    AnalyzeAssignment(post_stmt, post_stmt->GetStatementType(), depth + 1);
  }
}

/****************************
 * Analyzes a method call
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(MethodCall* method_call, const int depth)
{
#ifdef _DEBUG
  wstring msg = L"method/function call: class=" + method_call->GetVariableName() +
    L"; method=" + method_call->GetMethodName() + L"; call_type=" +
    ToString(method_call->GetCallType());
  Debug(msg, (static_cast<Expression*>(method_call))->GetLineNumber(), depth);
#endif

  //
  // new array call
  //
  if(method_call->GetCallType() == NEW_ARRAY_CALL) {
    AnalyzeNewArrayCall(method_call, depth);
  }
  //
  // enum call
  //
  else if(method_call->GetCallType() == ENUM_CALL) {
    const wstring variable_name = method_call->GetVariableName();
    const wstring method_name = method_call->GetMethodName();

    //
    // check library enum reference; fully qualified name
    //
    LibraryEnum* lib_eenum = linker->SearchEnumLibraries(variable_name + L"#" + method_name,
                                                         program->GetUses(current_class->GetFileName()));
    if(!lib_eenum) {
      lib_eenum = linker->SearchEnumLibraries(variable_name, program->GetUses(current_class->GetFileName()));
    }

    if(lib_eenum && method_call->GetMethodCall()) {
      const wstring item_name = method_call->GetMethodCall()->GetVariableName();
      ResolveEnumCall(lib_eenum, item_name, method_call);
    }
    else if(lib_eenum) {
      ResolveEnumCall(lib_eenum, method_name, method_call);
    }
    else {
      //
      // check program enum reference
      //
      wstring enum_name; wstring item_name;
      if(variable_name == current_class->GetName() && method_call->GetMethodCall()) {
        enum_name = method_name;
        item_name = method_call->GetMethodCall()->GetVariableName();
      }
      else {
        enum_name = variable_name;
        item_name = method_name;
      }

      // check fully qualified name
      Enum* eenum = SearchProgramEnums(enum_name + L"#" + item_name);
      if(eenum && method_call->GetMethodCall()) {
        item_name = method_call->GetMethodCall()->GetVariableName();
      }

      if(!eenum) {
        // local nested reference
        eenum = SearchProgramEnums(current_class->GetName() + L"#" + enum_name);
        if(!eenum) {
          // standalone reference
          eenum = SearchProgramEnums(enum_name);
        }
      }

      if(eenum) {
        EnumItem* item = eenum->GetItem(item_name);
        if(item) {
          if(method_call->GetMethodCall()) {
            method_call->GetMethodCall()->SetEnumItem(item, eenum->GetName());
            method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, eenum->GetName()), false);
            method_call->GetMethodCall()->SetEvalType(method_call->GetEvalType(), false);
          }
          else {
            method_call->SetEnumItem(item, eenum->GetName());
            method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, eenum->GetName()), false);
          }
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined enum item: '" + item_name + L"'");
        }
      }
      //
      // check '@self' reference
      //
      else if(enum_name == SELF_ID) {
        SymbolEntry* entry = GetEntry(item_name);
        if(entry && !entry->IsLocal() && !entry->IsStatic()) {
          AddMethodParameter(method_call, entry, depth + 1);
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid '@self' reference for variable: '" + item_name + L"'");
        }
      }
      //
      // check '@parent' reference
      //
      else if(enum_name == PARENT_ID) {
        SymbolEntry* entry = GetEntry(item_name, true);
        if(entry && !entry->IsLocal() && !entry->IsStatic()) {
          AddMethodParameter(method_call, entry, depth + 1);
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid '@parent' reference for variable: '" + item_name + L"'");
        }
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Undefined or incompatible enum type: '" +
                     ReplaceSubstring(enum_name, L"#", L"->") + L"'");
      }
    }

    // next call
    AnalyzeExpressionMethodCall(method_call, depth + 1);
  }
  //
  // parent call
  //
  else if(method_call->GetCallType() == PARENT_CALL) {
    AnalyzeParentCall(method_call, depth);
  }
  //
  // method/function
  //
  else {
    // static check
    const wstring variable_name = method_call->GetVariableName();
    SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
    if(entry && InvalidStatic(entry) && !capture_lambda) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference an instance variable from this context");
    }
    else if(method_call->GetVariable()) {
      AnalyzeVariable(method_call->GetVariable(), depth + 1);
    }
    else if(capture_lambda) {
      const wstring full_class_name = GetProgramLibraryClassName(variable_name);
      if(!HasProgramLibraryEnum(full_class_name) && !HasProgramLibraryClass(full_class_name)) {
	Variable* variable = TreeFactory::Instance()->MakeVariable(static_cast<Expression*>(method_call)->GetFileName(),
								   static_cast<Expression*>(method_call)->GetLineNumber(),
								   full_class_name);
	AnalyzeVariable(variable, depth + 1);
	method_call->SetVariable(variable);
	entry = GetEntry(method_call, full_class_name, depth);
      }
    }
    
    wstring encoding;
    // local call
    Class* klass = AnalyzeProgramMethodCall(method_call, encoding, depth);
    if(klass) {
      if(method_call->IsFunctionDefinition()) {
        AnalyzeFunctionReference(klass, method_call, encoding, depth);
      }
      else if(!method_call->GetMethod() && !method_call->GetMethod() && !method_call->GetLibraryMethod()) {
        AnalyzeMethodCall(klass, method_call, false, encoding, depth);
      }
      AnalyzeGenericMethodCall(method_call, depth+ 1);
      return;
    }
    // library call
    LibraryClass* lib_klass = AnalyzeLibraryMethodCall(method_call, encoding, depth);
    if(lib_klass) {
      if(method_call->IsFunctionDefinition()) {
        AnalyzeFunctionReference(lib_klass, method_call, encoding, depth);
      }
      else if(!method_call->GetMethod() && !method_call->GetMethod() && !method_call->GetLibraryMethod()) {
        AnalyzeMethodCall(lib_klass, method_call, false, encoding, false, depth);
      }
      AnalyzeGenericMethodCall(method_call, depth + 1);
      return;
    }

    if(entry) {
      if(method_call->GetVariable()) {
        bool is_enum_call = false;
        if(!AnalyzeExpressionMethodCall(method_call->GetVariable(), encoding,
           klass, lib_klass, is_enum_call)) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
        }
      }
      else {
        if(!AnalyzeExpressionMethodCall(entry, encoding, klass, lib_klass)) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
        }
      }

      // check method call
      if(klass) {
        AnalyzeMethodCall(klass, method_call, false, encoding, depth);
      }
      else if(lib_klass) {
        AnalyzeMethodCall(lib_klass, method_call, false, encoding, false, depth);
      }
      else {
        if(!variable_name.empty()) {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined class: '" + variable_name + L"'");
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Undefined class or method call: '" + method_call->GetMethodName() + L"'");
        }
      }
    }
    else {
      if(!variable_name.empty()) {
        ProcessError(static_cast<Expression*>(method_call), L"Undefined class: '" + variable_name + L"'");
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Undefined class or method call: '" + method_call->GetMethodName() + L"'");
      }
    }
  }
}

void ContextAnalyzer::ValidateGenericConcreteMapping(const vector<Type*> concrete_types, LibraryClass* lib_klass, ParseNode* node)
{
  const vector<LibraryClass*> class_generics = lib_klass->GetGenericClasses();
  if(class_generics.size() != concrete_types.size()) {
    ProcessError(node, L"Cannot utilize an unqualified instance of class: '" + lib_klass->GetName() + L"'");
  }
  // check individual types
  if(class_generics.size() == concrete_types.size()) {
    for(size_t i = 0; i < concrete_types.size(); ++i) {
      Type* concrete_type = concrete_types[i];
      LibraryClass* class_generic = class_generics[i];
      if(class_generic->HasGenericInterface()) {
        const wstring backing_inf_name = class_generic->GetGenericInterface()->GetName();
        const wstring concrete_name = concrete_type->GetName();
        Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
        if(GetProgramLibraryClass(concrete_type, inf_klass, inf_lib_klass)) {
          if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
            ProcessError(node, L"Concrete class: '" + concrete_name +
                         L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
          }
        }
        else {
          inf_klass = current_class->GetGenericClass(concrete_name);
          if(inf_klass) {
            if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
              ProcessError(node, L"Concrete class: '" + concrete_name +
                           L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
            }
          }
          else {
            ProcessError(node, L"Undefined class or interface: '" + concrete_name + L"'");
          }
        }
      }
    }
  }
}

void ContextAnalyzer::ValidateGenericConcreteMapping(const vector<Type*> concrete_types, Class* klass, ParseNode* node)
{
  const vector<Class*> class_generics = klass->GetGenericClasses();
  if(class_generics.size() != concrete_types.size()) {
    ProcessError(node, L"Cannot create an unqualified instance of class: '" + klass->GetName() + L"'");
  }
  // check individual types
  if(class_generics.size() == concrete_types.size()) {
    for(size_t i = 0; i < concrete_types.size(); ++i) {
      Type* concrete_type = concrete_types[i];
      ResolveClassEnumType(concrete_type);

      Class* class_generic = class_generics[i];
      if(class_generic->HasGenericInterface()) {
        const wstring backing_inf_name = GetProgramLibraryClassName(class_generic->GetGenericInterface()->GetName());
        const wstring concrete_name = concrete_type->GetName();
        Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
        if(GetProgramLibraryClass(concrete_type, inf_klass, inf_lib_klass)) {
          if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
            ProcessError(node, L"Concrete class: '" + concrete_name +
                         L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
          }
        }
        else {
          inf_klass = current_class->GetGenericClass(concrete_name);
          if(inf_klass) {
            if(!ValidDownCast(backing_inf_name, inf_klass, inf_lib_klass)) {
              ProcessError(node, L"Concrete class: '" + concrete_name +
                           L"' is incompatible with backing class/interface '" + backing_inf_name + L"'");
            }
          }
          else {
            ProcessError(node, L"Undefined class or interface: '" + concrete_name + L"'");
          }
        }
      }
    }
  }
}

void ContextAnalyzer::ValidateGenericBacking(Type* type, const wstring backing_name, Expression * expression)
{
  const wstring concrete_name = type->GetName();
  Class* inf_klass = nullptr; LibraryClass* inf_lib_klass = nullptr;
  if(GetProgramLibraryClass(type, inf_klass, inf_lib_klass)) {
    if(!ValidDownCast(backing_name, inf_klass, inf_lib_klass) && !ClassEquals(backing_name, inf_klass, inf_lib_klass)) {
      ProcessError(expression, L"Concrete class: '" + concrete_name +
                   L"' is incompatible with backing class/interface '" + backing_name + L"'");
    }
  }
  else if((inf_klass = current_class->GetGenericClass(concrete_name))) {
    if(!ValidDownCast(backing_name, inf_klass, inf_lib_klass) && !ClassEquals(backing_name, inf_klass, inf_lib_klass)) {
      ProcessError(expression, L"Concrete class: '" + concrete_name +
                   L"' is incompatible with backing class/interface '" + backing_name + L"'");
    }
  }
  else if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* mthd_call = static_cast<MethodCall*>(expression);
    if(mthd_call->GetConcreteTypes().empty() && mthd_call->GetEntry()) {
      vector<Type*> concrete_types = mthd_call->GetEntry()->GetType()->GetGenerics();
      vector<Type*> concrete_copies;
      for(size_t i = 0; i < concrete_types.size(); ++i) {
        concrete_copies.push_back(TypeFactory::Instance()->MakeType(concrete_types[i]));
      }
      mthd_call->SetConcreteTypes(concrete_copies);
    }
    else {
      ProcessError(expression, L"Undefined class or interface: '" + concrete_name + L"'");
    }
  }
  else {
    ProcessError(expression, L"Undefined class or interface: '" + concrete_name + L"'");
  }
}
/****************************
 * Validates an expression
 * method call
 ****************************/
bool ContextAnalyzer::AnalyzeExpressionMethodCall(Expression* expression, wstring &encoding,
                                                  Class* &klass, LibraryClass* &lib_klass,
                                                  bool &is_enum_call)
{
  Type* type;
  // process cast
  if(expression->GetCastType()) {
    if(expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expression)->GetVariable()) {
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression->GetMethodCall(), 0);
        expression = expression->GetMethodCall();
      }
      type = expression->GetEvalType();
    }
    else if(expression->GetExpressionType() == VAR_EXPR) {
      if(static_cast<Variable*>(expression)->GetIndices()) {
        ProcessError(expression, L"Unable to make a method call from an indexed array element");
        return false;
      }
      type = expression->GetCastType();
    }
    else {
      type = expression->GetCastType();
    }
  }
  // process non-cast
  else {
    type = expression->GetEvalType();
  }

  if(expression->GetExpressionType() == STAT_ARY_EXPR) {
    ProcessError(expression, L"Unable to make method calls on static arrays");
    return false;
  }

  if(type) {
    const int dimension = IsScalar(expression, false) ? 0 : type->GetDimension();
    return AnalyzeExpressionMethodCall(type, dimension, encoding, klass, lib_klass, is_enum_call);
  }

  return false;
}

/****************************
 * Validates an expression
 * method call
 ****************************/
bool ContextAnalyzer::AnalyzeExpressionMethodCall(SymbolEntry* entry, wstring &encoding,
                                                  Class* &klass, LibraryClass* &lib_klass)
{
  Type* type = entry->GetType();
  if(type) {
    bool is_enum_call = false;
    return AnalyzeExpressionMethodCall(type, type->GetDimension(),
                                       encoding, klass, lib_klass, is_enum_call);
  }

  return false;
}

/****************************
 * Validates an expression
 * method call
 ****************************/
bool ContextAnalyzer::AnalyzeExpressionMethodCall(Type* type, const int dimension,
                                                  wstring &encoding, Class* &klass,
                                                  LibraryClass* &lib_klass, bool& is_enum_call)
{
  switch(type->GetType()) {
  case BOOLEAN_TYPE:
    klass = program->GetClass(BOOL_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(BOOL_CLASS_ID, program->GetUses(current_class->GetFileName()));
    encoding = L"l";
    break;

  case VAR_TYPE:
  case NIL_TYPE:
    return false;

  case BYTE_TYPE:
    klass = program->GetClass(BYTE_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(BYTE_CLASS_ID, program->GetUses(current_class->GetFileName()));
    encoding = L"b";
    break;

  case CHAR_TYPE:
    klass = program->GetClass(CHAR_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(CHAR_CLASS_ID, program->GetUses(current_class->GetFileName()));
    encoding = L"c";
    break;

  case INT_TYPE:
    klass = program->GetClass(INT_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(INT_CLASS_ID, program->GetUses(current_class->GetFileName()));
    encoding = L"i";
    break;

  case FLOAT_TYPE:
    klass = program->GetClass(FLOAT_CLASS_ID);
    lib_klass = linker->SearchClassLibraries(FLOAT_CLASS_ID, program->GetUses(current_class->GetFileName()));
    encoding = L"f";
    break;

  case CLASS_TYPE: {
    if(dimension > 0 && type->GetDimension() > 0) {
      klass = program->GetClass(BASE_ARRAY_CLASS_ID);
      lib_klass = linker->SearchClassLibraries(BASE_ARRAY_CLASS_ID, program->GetUses(current_class->GetFileName()));
      encoding = L"o.System.Base";
    }
    else {
      const wstring &cls_name = type->GetName();
      klass = SearchProgramClasses(cls_name);
      lib_klass = linker->SearchClassLibraries(cls_name, program->GetUses(current_class->GetFileName()));

      if(!klass && !lib_klass) {
        if(HasProgramLibraryEnum(cls_name)) {
          klass = program->GetClass(INT_CLASS_ID);
          lib_klass = linker->SearchClassLibraries(INT_CLASS_ID, program->GetUses(current_class->GetFileName()));
          encoding = L"i,";
          is_enum_call = true;
        }
      }
    }
  }
    break;

  default:
    return false;
  }

  // dimension
  for(int i = 0; i < dimension; ++i) {
    encoding += L'*';
  }

  if(type->GetType() != CLASS_TYPE) {
    encoding += L",";
  }

  return true;
}

/****************************
 * Analyzes a new array method
 * call
 ****************************/
void ContextAnalyzer::AnalyzeNewArrayCall(MethodCall* method_call, const int depth)
{
  Class* generic_class = current_class->GetGenericClass(method_call->GetEvalType()->GetName());
  if(generic_class && generic_class->HasGenericInterface() && method_call->GetEvalType()) {
    const int dimension = method_call->GetEvalType()->GetDimension();
    method_call->SetEvalType(generic_class->GetGenericInterface(), false);
    method_call->GetEvalType()->SetDimension(dimension);
  }

  // get parameters
  ExpressionList* call_params = method_call->GetCallingParameters();
  AnalyzeExpressions(call_params, depth + 1);
  // check indexes
  vector<Expression*> expressions = call_params->GetExpressions();
  if(expressions.size() == 0) {
    ProcessError(static_cast<Expression*>(method_call), L"Empty array index");
  }
  // validate array parameters
  for(size_t i = 0; i < expressions.size(); ++i) {
    Expression* expression = expressions[i];
    AnalyzeExpression(expression, depth + 1);
    Type* type = GetExpressionType(expression, depth + 1);
    if(type) {
      switch(type->GetType()) {
      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        break;

      case CLASS_TYPE:
        if(!IsEnumExpression(expression)) {
          ProcessError(expression, L"Array index type must be an Integer, Char, Byte or Enum");
        }
        break;

      default:
        ProcessError(expression, L"Array index type must be an Integer, Char, Byte or Enum");
        break;
      }
    }
  }
  // generic array type
  if(method_call->HasConcreteTypes() && method_call->GetEvalType()) {
    Class* generic_klass = nullptr; LibraryClass* generic_lib_klass = nullptr;
    if(GetProgramLibraryClass(method_call->GetEvalType(), generic_klass, generic_lib_klass)) {
      const vector<Type*> concrete_types = GetConcreteTypes(method_call);
      if(generic_klass) {
        const vector<Class*> generic_classes = generic_klass->GetGenericClasses();
        if(concrete_types.size() == generic_classes.size()) {
          method_call->GetEvalType()->SetGenerics(concrete_types);
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
        }
      }
      else {
        const vector<LibraryClass*> generic_classes = generic_lib_klass->GetGenericClasses();
        if(concrete_types.size() == generic_classes.size()) {
          method_call->GetEvalType()->SetGenerics(concrete_types);
        }
        else {
          ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
        }
      }
    }
  }
}

/*********************************
 * Analyzes a parent method call
 *********************************/
void ContextAnalyzer::AnalyzeParentCall(MethodCall* method_call, const int depth)
{
  // get parameters
  ExpressionList* call_params = method_call->GetCallingParameters();
  AnalyzeExpressions(call_params, depth + 1);

  Class* parent = current_class->GetParent();
  if(parent) {
    wstring encoding;
    AnalyzeMethodCall(parent, method_call, false, encoding, depth);
  }
  else {
    LibraryClass* lib_parent = current_class->GetLibraryParent();
    if(lib_parent) {
      wstring encoding;
      AnalyzeMethodCall(lib_parent, method_call, false, encoding, true, depth);
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Class has no parent");
    }
  }
}

/****************************
 * Analyzes generic method call
 ****************************/
void ContextAnalyzer::AnalyzeGenericMethodCall(MethodCall* method_call, const int depth)
{
  if(method_call->GetEntry() || method_call->GetVariable()) {
    vector<Type*> entry_generics;
    if(method_call->GetEntry()) {
      entry_generics = method_call->GetEntry()->GetType()->GetGenerics();
    }
    else if(method_call->GetVariable() && method_call->GetVariable()->GetEntry()) {
      entry_generics = method_call->GetVariable()->GetEntry()->GetType()->GetGenerics();
    }

    if(!entry_generics.empty()) {
      while(method_call && method_call->GetEvalType()) {
        if(method_call->GetPreviousExpression()) {
          // TODO: set prior
          entry_generics = method_call->GetPreviousExpression()->GetEvalType()->GetGenerics();
        }

        vector<Type*> eval_types = method_call->GetEvalType()->GetGenerics();
        if(method_call->GetMethod()) {
          Class* klass = method_call->GetMethod()->GetClass();
          vector<Class*> klass_generics = klass->GetGenericClasses();
          if(entry_generics.size() >= klass_generics.size()) {
            // TODO
          }
          else {
            ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
          }
        }
        else if(method_call->GetLibraryMethod()) {
          LibraryClass* lib_klass = method_call->GetLibraryMethod()->GetLibraryClass();
          vector<LibraryClass*> klass_generics = lib_klass->GetGenericClasses();
          if(entry_generics.size() >= klass_generics.size()) {            
            vector<Type*> mapped_types;
            if(klass_generics.size() == 1) {
              mapped_types.push_back(entry_generics.front());
            }
            else {
              // build map
              map<wstring, Type*> type_map;
              for(size_t i = 0; i < klass_generics.size(); ++i) {
                type_map[klass_generics[i]->GetName()] = entry_generics[i];
              }
              // ...              
              for(size_t i = 0; i < eval_types.size(); ++i) {
                Type* mapped_type = type_map[eval_types[i]->GetName()];
                if(mapped_type) {
                  mapped_types.push_back(mapped_type);
                }
              }
            }
            // update eval type
            method_call->GetEvalType()->SetGenerics(mapped_types);
          }
          else {
            ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
          }

          method_call = method_call->GetMethodCall();
        }
      }
    }
  }
}

/****************************
 * Analyzes a method call
 ****************************/
void ContextAnalyzer::AnalyzeExpressionMethodCall(Expression* expression, const int depth)
{
  MethodCall* method_call = expression->GetMethodCall();
  if(method_call && method_call->GetCallType() != ENUM_CALL) {
    wstring encoding;
    Class* klass = nullptr;
    LibraryClass* lib_klass = nullptr;

    // check expression class
    bool is_enum_call = false;
    if(!AnalyzeExpressionMethodCall(expression, encoding, klass, lib_klass, is_enum_call)) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid class type or assignment");
    }
    method_call->SetEnumCall(is_enum_call);

    // check methods
    if(klass) {
      AnalyzeMethodCall(klass, method_call, true, encoding, depth);
    }
    else if(lib_klass) {
      AnalyzeMethodCall(lib_klass, method_call, true, encoding, false, depth);
    }
    else {
      if(expression->GetEvalType()) {
        ProcessError(static_cast<Expression*>(method_call), L"Undefined class reference: '" +
         expression->GetEvalType()->GetName() +
         L"'\n\tIf external reference to generic ensure it has been typed");
      }
      else {
        ProcessError(static_cast<Expression*>(method_call),
                     L"Undefined class reference.\n\tIf external reference to generic ensure it has been typed");
      }
    }
  }
}

/*********************************
 * Analyzes a method call.  This
 * is method call within the source
 * program.
 *********************************/
Class* ContextAnalyzer::AnalyzeProgramMethodCall(MethodCall* method_call, wstring &encoding, const int depth)
{
  Class* klass = nullptr;

  // method within the same class
  const wstring variable_name = method_call->GetVariableName();
  if(method_call->GetMethodName().size() == 0) {
    klass = SearchProgramClasses(current_class->GetName());
  }
  else {
    // external method
    SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
    if(entry && entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
      if(entry->GetType()->GetDimension() > 0 &&
        (!method_call->GetVariable() ||
         !method_call->GetVariable()->GetIndices())) {
        klass = program->GetClass(BASE_ARRAY_CLASS_ID);
        encoding = L"o.System.Base";
        for(int i = 0; i < entry->GetType()->GetDimension(); ++i) {
          encoding += L"*";
        }
        encoding += L",";

      }
      else if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() &&
              method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
        klass = SearchProgramClasses(method_call->GetVariable()->GetCastType()->GetName());
      }
      else {
        klass = SearchProgramClasses(entry->GetType()->GetName());
      }
    }
    // static method call
    if(!klass) {
      klass = SearchProgramClasses(variable_name);
    }
  }

  if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() &&
     method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
    AnalyzeClassCast(method_call->GetVariable()->GetCastType(), method_call, depth + 1);
  }
  // intermediate cast type
  else if(method_call->GetCastType() && method_call->GetCastType()->GetType() == CLASS_TYPE) {
    AnalyzeVariableCast(method_call->GetCastType(), method_call);
  }

  return klass;
}

/*********************************
 * Analyzes a method call.  This
 * is method call within a linked
 * library
 *********************************/
LibraryClass* ContextAnalyzer::AnalyzeLibraryMethodCall(MethodCall* method_call, wstring &encoding, const int depth)
{
  LibraryClass* klass = nullptr;
  const wstring variable_name = method_call->GetVariableName();

  // external method
  SymbolEntry* entry = GetEntry(method_call, variable_name, depth);
  if(entry && entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
    // array type
    if(entry->GetType()->GetDimension() > 0 &&
      (!method_call->GetVariable() ||
       !method_call->GetVariable()->GetIndices())) {

      klass = linker->SearchClassLibraries(BASE_ARRAY_CLASS_ID, program->GetUses(current_class->GetFileName()));
      encoding = L"o.System.Base";
      for(int i = 0; i < entry->GetType()->GetDimension(); ++i) {
        encoding += L"*";
      }
      encoding += L",";
    }
    // cast type
    else if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() &&
            method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
      klass = linker->SearchClassLibraries(method_call->GetVariable()->GetCastType()->GetName(),
                                           program->GetUses(current_class->GetFileName()));
      method_call->SetTypes(entry->GetType());
    }
    else {
      klass = linker->SearchClassLibraries(entry->GetType()->GetName(), program->GetUses(current_class->GetFileName()));
    }
  }
  // static method call
  if(!klass) {
    klass = linker->SearchClassLibraries(variable_name, program->GetUses(current_class->GetFileName()));
  }

  // cast type
  if(method_call->GetVariable() && method_call->GetVariable()->GetCastType() &&
     method_call->GetVariable()->GetCastType()->GetType() == CLASS_TYPE) {
    AnalyzeClassCast(method_call->GetVariable()->GetCastType(), method_call, depth + 1);
  }
  // intermediate cast type
  else if(method_call->GetCastType() && method_call->GetCastType()->GetType() == CLASS_TYPE) {
    AnalyzeVariableCast(method_call->GetCastType(), method_call);
  }

  return klass;
}

/*********************************
 * Resolve method call parameter
 *********************************/
int ContextAnalyzer::MatchCallingParameter(Expression* calling_param, Type* method_type, Class* klass, 
                                           LibraryClass* lib_klass, const int depth)
{
  // get calling type
  Type* calling_type = GetExpressionType(calling_param, depth + 1);

  // determine if there's a mapping from calling type to method type
  if(calling_type && method_type) {
    // processing an array
    if(!IsScalar(calling_param)) {
      if(calling_type->GetType() == method_type->GetType()) {
        // class/enum arrays
        if(calling_type->GetType() == CLASS_TYPE &&
           IsClassEnumParameterMatch(calling_type, method_type) &&
           calling_type->GetDimension() == method_type->GetDimension()) {
          return 0;
        }
        // basic arrays
        else if(calling_type->GetDimension() == method_type->GetDimension()) {
          return 0;
        }
      }

      return -1;
    }
    else {
      // look for an exact match
      if(calling_type->GetType() != CLASS_TYPE && method_type->GetType() != CLASS_TYPE &&
         calling_type->GetType() != FUNC_TYPE && method_type->GetType() != FUNC_TYPE &&
         method_type->GetDimension() == 0 && calling_type->GetType() == method_type->GetType()) {
        return 0;
      }

      // looks for a relative match
      if(method_type->GetDimension() == 0) {
        if(IsHolderType(method_type->GetName())) {
          switch (calling_type->GetType()) {
          case BYTE_TYPE:
            calling_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.ByteHolder");
            break;
          case CHAR_TYPE:
            calling_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.CharHolder");
            break;
          case INT_TYPE:
            calling_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.IntHolder");
            break;
          case FLOAT_TYPE:
            calling_type = TypeFactory::Instance()->MakeType(CLASS_TYPE, L"System.FloatHolder");
            break;
          }
        }

        switch(calling_type->GetType()) {
        case NIL_TYPE:
          if(method_type->GetType() == CLASS_TYPE) {
            return 1;
          }
          return -1;

        case BOOLEAN_TYPE:
          return method_type->GetType() == BOOLEAN_TYPE ? 0 : -1;

        case BYTE_TYPE:
        case CHAR_TYPE:
        case INT_TYPE:
        case FLOAT_TYPE:
          switch(method_type->GetType()) {
          case BYTE_TYPE:
          case CHAR_TYPE:
          case INT_TYPE:
          case FLOAT_TYPE:
            return 1;

          default:
            return -1;
          }

        case CLASS_TYPE: {
          if(method_type->GetType() == CLASS_TYPE) {
            // calculate exact match
            if(IsClassEnumParameterMatch(calling_type, method_type)) {
              if(calling_type->HasGenerics() || method_type->HasGenerics()) {
                if(CheckGenericEqualTypes(calling_type, method_type, calling_param, true)) {
                  return 0;
                }
                return -1;
              }
              return 0;
            }
            // calculate relative match
            const wstring &from_klass_name = calling_type->GetName();
            Class* from_klass = SearchProgramClasses(from_klass_name);
            LibraryClass* from_lib_klass = linker->SearchClassLibraries(from_klass_name, program->GetUses(current_class->GetFileName()));

            const wstring &to_klass_name = method_type->GetName();
            Class* to_klass = SearchProgramClasses(to_klass_name);
            if(to_klass) {
              return ValidDownCast(to_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
            }

            LibraryClass* to_lib_klass = linker->SearchClassLibraries(to_klass_name, program->GetUses(current_class->GetFileName()));
            if(to_lib_klass) {
              return ValidDownCast(to_lib_klass->GetName(), from_klass, from_lib_klass) ? 1 : -1;
            }
          }
          else if(method_type->GetType() == INT_TYPE) {
            // program
            if(program->GetEnum(calling_type->GetName()) ||
               linker->SearchEnumLibraries(calling_type->GetName(), program->GetUses())) {
              return 1;
            }
          }

          return -1;
        }

        case FUNC_TYPE: {
          const wstring calling_type_name = calling_type->GetName();
          wstring method_type_name = method_type->GetName();
          if(method_type_name.size() == 0) {
            AnalyzeVariableFunctionParameters(method_type, calling_param);
            method_type_name = L"m." + EncodeFunctionType(method_type->GetFunctionParameters(),
                                                          method_type->GetFunctionReturn());
            method_type->SetName(method_type_name);
          }

          return calling_type_name == method_type_name ? 0 : -1;
        }

        case ALIAS_TYPE:
        case VAR_TYPE:
          return -1;
        }
      }
    }
  }

  return -1;
}

/****************************
 * Resolves method calls
 ****************************/
Method* ContextAnalyzer::ResolveMethodCall(Class* klass, MethodCall* method_call, const int depth)
{
  const wstring method_name = method_call->GetMethodName();
  ExpressionList* calling_params = method_call->GetCallingParameters();
  vector<Expression*> expr_params = calling_params->GetExpressions();
  vector<Method*> candidates = klass->GetAllUnqualifiedMethods(method_name);

  // save all valid candidates
  vector<MethodCallSelection*> matches;
  for(size_t i = 0; i < candidates.size(); ++i) {
    // match parameter sizes
    vector<Declaration*> method_parms = candidates[i]->GetDeclarations()->GetDeclarations();

    if(expr_params.size() == method_parms.size()) {
      // box and unbox parameters
      vector<Expression*> boxed_resolved_params;
      for(size_t j = 0; j < expr_params.size(); ++j) {
        // cannot be set to method, need to preserve test against other selections
        Expression* expr_param = expr_params[j];
        Type* expr_type = expr_param->GetEvalType();
        Type* method_type = ResolveGenericType(method_parms[j]->GetEntry()->GetType(), method_call, klass, nullptr, false);

        Expression* boxed_param = BoxExpression(method_type, expr_param, depth);
        if(boxed_param) {
          boxed_resolved_params.push_back(boxed_param);
        }
        else if((boxed_param = UnboxingExpression(expr_type, expr_param, false, depth))) {
          boxed_resolved_params.push_back(boxed_param);
        }
        // add default
        if(!boxed_param) {
          boxed_resolved_params.push_back(expr_param);
        }
      }

#ifdef _DEBUG
      assert(boxed_resolved_params.size() == expr_params.size());
#endif

      MethodCallSelection* match = new MethodCallSelection(candidates[i], boxed_resolved_params);
      for(size_t j = 0; j < boxed_resolved_params.size(); ++j) {
        Type* method_type = ResolveGenericType(method_parms[j]->GetEntry()->GetType(), method_call, klass, nullptr, false);
        // add parameter match
        const int compare = MatchCallingParameter(boxed_resolved_params[j], method_type, klass, nullptr, depth);
        match->AddParameterMatch(compare);
      }
      matches.push_back(match);
    }
  }

  // evaluate matches
  MethodCallSelector selector(method_call, matches);
  Method* method = selector.GetSelection();

  if(method) {
    // check casts on final candidate
    vector<Declaration*> method_parms = method->GetDeclarations()->GetDeclarations();
    for(size_t j = 0; j < expr_params.size(); ++j) {
      Expression* expression = expr_params[j];
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        expression = expression->GetMethodCall();
      }
      // erase/resolve type
      Type* left = ResolveGenericType(method_parms[j]->GetEntry()->GetType(), method_call, klass, nullptr, false);
      AnalyzeRightCast(left, expression, IsScalar(expression), depth + 1);
    }
  }
  else {
    vector<Method*> alt_mthds = selector.GetAlternativeMethods();
    Method* derived_method = DerivedLambdaFunction(alt_mthds);
    if(derived_method) {
      return derived_method;
    }
    else if(alt_mthds.size()) {
      alt_error_method_names = selector.GetAlternativeMethodNames();
    }
  }

  return method;
}

/****************************
 * Analyzes a method call.  This
 * is method call within the source
 * program.
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(Class* klass, MethodCall* method_call, bool is_expr, wstring &encoding, const int depth)
 {
#ifdef _DEBUG
  GetLogger() << L"Checking program class call: |" << klass->GetName() << L':' 
    << (method_call->GetMethodName().size() > 0 ? method_call->GetMethodName() : method_call->GetVariableName())
    << L"|" << endl;
#endif

  // calling parameters
  ExpressionList* call_params = method_call->GetCallingParameters();

  // lambda inferred type
  CheckLambdaInferredTypes(method_call, depth + 1);

  AnalyzeExpressions(call_params, depth + 1);

  // note: find system based methods and call with function parameters (i.e. $Int, $Float)
  Method* method = ResolveMethodCall(klass, method_call, depth);
  if(!method) {
    const wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() + L':' + encoding +
      EncodeMethodCall(method_call->GetCallingParameters(), depth);
    method = klass->GetMethod(encoded_name);
  }

  if(!method) {
    if(klass->GetParent()) {
      Class* parent = klass->GetParent();
      method_call->SetOriginalClass(klass);
      wstring encoding;
      AnalyzeMethodCall(parent, method_call, is_expr, encoding, depth + 1);
      return;
    }
    else if(klass->GetLibraryParent()) {
      LibraryClass* lib_parent = klass->GetLibraryParent();
      method_call->SetOriginalClass(klass);
      wstring encoding;
      AnalyzeMethodCall(lib_parent, method_call, is_expr, encoding, true, depth + 1);
      return;
    }
    else {
      AnalyzeVariableFunctionCall(method_call, depth + 1);
      return;
    }
  }

  // found program method
  if(method) {
    // look for implicit casts
    vector<Declaration*> mthd_params = method->GetDeclarations()->GetDeclarations();
    vector<Expression*> expressions = call_params->GetExpressions();

#ifndef _SYSTEM
    if(mthd_params.size() != expressions.size()) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid method call context");
      return;
    }
#endif

    for(size_t i = 0; i < mthd_params.size(); ++i) {
      AnalyzeDeclaration(mthd_params[i], klass, depth + 1);
    }

    Expression* expression;
    for(size_t i = 0; i < expressions.size(); ++i) {
      expression = expressions[i];
      // find eval type
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        expression = expression->GetMethodCall();
      }
      // check cast
      if(mthd_params[i]->GetEntry()) {
        if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
           expression->GetEvalType()->GetType() == NIL_TYPE) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid operation with 'Nil' value");
        }
        // check generic parameters for call
        Type* left = ResolveGenericType(mthd_params[i]->GetEntry()->GetType(), method_call, klass, nullptr, false);
        AnalyzeRightCast(left, expression->GetEvalType(), expression, IsScalar(expression), depth + 1);
      }
    }

    // public/private check
    if(method->GetClass() != current_method->GetClass() && !method->IsStatic() &&
      (method->GetMethodType() == PRIVATE_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD)) {
      bool found = false;
      Class* parent = current_method->GetClass()->GetParent();
      while(parent && !found) {
        if(method->GetClass() == parent) {
          found = true;
        }
        // update
        parent = parent->GetParent();
      }

      if(!found) {
        ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a private method from this context");
      }
    }
    
    // check private class scope
    const wstring bundle_name = klass->GetBundleName();
    if(!klass->IsPublic() && current_class->GetBundleName() != bundle_name) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot access private class '" + klass->GetName() + L"' from this bundle scope");
    }

    // static check
    if(!is_expr && InvalidStatic(method_call, method)) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot reference an instance method from this context");
    }
    
    // cannot create an instance of a virtual class
    if((method->GetMethodType() == NEW_PUBLIC_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD) &&
       klass->IsVirtual() && current_class->GetParent() != klass) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot create an instance of a virtual class or interface");
    }
    
    // associate method
    klass->SetCalled(true);
    method_call->SetOriginalClass(klass);
    method_call->SetMethod(method);
    
    // map concrete to generic types
    const bool is_new = method->GetMethodType() == NEW_PUBLIC_METHOD || method->GetMethodType() == NEW_PRIVATE_METHOD;
    const bool same_cls_return = ClassEquals(method->GetReturn()->GetName(), klass, nullptr);
    if((is_new || same_cls_return) && klass->HasGenerics()) {
      const vector<Class*> class_generics = klass->GetGenericClasses();      
      vector<Type*> concrete_types = GetConcreteTypes(method_call);
      if(class_generics.size() != concrete_types.size()) {
        ProcessError(static_cast<Expression*>(method_call), L"Cannot create an unqualified instance of class: '" + klass->GetName() + L"'");
      }
      // check individual types
      if(class_generics.size() == concrete_types.size()) {
        for(size_t i = 0; i < concrete_types.size(); ++i) {
          Type* concrete_type = concrete_types[i];
          Class* class_generic = class_generics[i];
          if(class_generic->HasGenericInterface()) {
            Type* backing_type = class_generic->GetGenericInterface();
            // backing type
            ResolveClassEnumType(backing_type);
            const wstring backing_name = backing_type->GetName();
            // concrete type
            ResolveClassEnumType(concrete_type);
            // validate backing
            ValidateGenericBacking(concrete_type, backing_name, static_cast<Expression*>(method_call));
          }
        }
      }
      method_call->GetEvalType()->SetGenerics(concrete_types);
    }

    // resolve generic to concrete, if needed
    Type* eval_type = method_call->GetEvalType();
    if(klass->HasGenerics()) {
      eval_type = ResolveGenericType(eval_type, method_call, klass, nullptr, true);
      method_call->SetEvalType(eval_type, false);
    }

    if(eval_type->GetType() == CLASS_TYPE && !ResolveClassEnumType(eval_type, klass)) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined class or enum: '" +
                   ReplaceSubstring(eval_type->GetName(), L"#", L"->") + L"'");
    }

    // set subsequent call type
    if(method_call->GetMethodCall()) {
      Type* expr_type = ResolveGenericType(method->GetReturn(), method_call, klass, nullptr, true);
      method_call->GetMethodCall()->SetEvalType(expr_type, false);
    }

    // enum check
    if(method_call->GetMethodCall() && method_call->GetMethodCall()->GetCallType() == ENUM_CALL) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid enum reference");
    }

    // next call
    AnalyzeExpressionMethodCall(method_call, depth + 1);
  }
  else {
    const wstring &mthd_name = method_call->GetMethodName();
    const wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      wstring message = L"Undefined function/method call: '" +
        mthd_name + L"(..)'\n\tEnsure the object and it's calling parameters are properly casted";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
    else {
      wstring message = L"Undefined function/method call: '" +
        var_name + L"(..)'\n\tEnsure the object and it's calling parameters are properly casted";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
  }
}

/****************************
 * Resolves library method calls
 ****************************/
LibraryMethod* ContextAnalyzer::ResolveMethodCall(LibraryClass* klass, MethodCall* method_call, const int depth)
{
  const wstring &method_name = method_call->GetMethodName();
  ExpressionList* calling_params = method_call->GetCallingParameters();
  vector<Expression*> expr_params = calling_params->GetExpressions();
  vector<LibraryMethod*> candidates = klass->GetUnqualifiedMethods(method_name);

  // save all valid candidates
  vector<LibraryMethodCallSelection*> matches;
  for(size_t i = 0; i < candidates.size(); ++i) {
    // match parameter sizes
    vector<Type*> method_parms = candidates[i]->GetDeclarationTypes();
    if(expr_params.size() == method_parms.size()) {
      // box and unbox parameters
      vector<Expression*> boxed_resolved_params;
      for(size_t j = 0; j < expr_params.size(); ++j) {
        Expression* expr_param = expr_params[j];
        Type* expr_type = expr_param->GetEvalType();
        Type* method_type = ResolveGenericType(method_parms[j], method_call, nullptr, klass, false);

        Expression* boxed_param = BoxExpression(method_type, expr_param, depth);
        if(boxed_param) {
          boxed_resolved_params.push_back(boxed_param);
        }
        else if((boxed_param = UnboxingExpression(expr_type, expr_param, false, depth))) {
          boxed_resolved_params.push_back(boxed_param);
        }
        // add default
        if(!boxed_param) {
          boxed_resolved_params.push_back(expr_param);
        }
      }

#ifdef _DEBUG
      assert(boxed_resolved_params.size() == expr_params.size());
#endif

      LibraryMethodCallSelection* match = new LibraryMethodCallSelection(candidates[i], boxed_resolved_params);
      for(size_t j = 0; j < boxed_resolved_params.size(); ++j) {
        Type* method_type = ResolveGenericType(method_parms[j], method_call, nullptr, klass, false);
        const int compare = MatchCallingParameter(boxed_resolved_params[j], method_type, nullptr, klass, depth);
        match->AddParameterMatch(compare);
      }
      matches.push_back(match);
    }
  }

  // evaluate matches
  LibraryMethodCallSelector selector(method_call, matches);
  LibraryMethod* lib_method = selector.GetSelection();
  
  if(lib_method) {
    // check casts on final candidate
    vector<Type*> method_parms = lib_method->GetDeclarationTypes();
    for(size_t j = 0; j < expr_params.size(); ++j) {
      Expression* expression = expr_params[j];
      while(expression->GetMethodCall()) {
        AnalyzeExpressionMethodCall(expression, depth + 1);
        if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
           expression->GetEvalType()->GetType() == NIL_TYPE) {
          ProcessError(static_cast<Expression*>(method_call), L"Invalid operation with 'Nil' value");
        }
        expression = expression->GetMethodCall();
      }
      // map generic to concrete type, if needed
      Type* left = ResolveGenericType(method_parms[j], method_call, nullptr, klass, false);
      AnalyzeRightCast(left, expression, IsScalar(expression), depth + 1);
    }
  }
  else {
    vector<LibraryMethod*> alt_mthds = selector.GetAlternativeMethods();
    LibraryMethod* derived_method = DerivedLambdaFunction(alt_mthds);
    if(derived_method) {
      return derived_method;
    }
    else if(alt_mthds.size()) {
      alt_error_method_names = selector.GetAlternativeMethodNames();
    }
  }

  return lib_method;
}

/****************************
 * Analyzes a method call.  This
 * is method call within a linked
 * library
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(LibraryClass* klass, MethodCall* method_call, bool is_expr, 
                                        wstring &encoding, bool is_parent, const int depth)
{
#ifdef _DEBUG
  GetLogger() << L"Checking library encoded name: |" << klass->GetName() << L':' << method_call->GetMethodName() << L"|" << endl;
#endif

  ExpressionList* call_params = method_call->GetCallingParameters();

  // lambda inferred type
  CheckLambdaInferredTypes(method_call, depth + 1);
  
  AnalyzeExpressions(call_params, depth + 1);
  LibraryMethod* lib_method = ResolveMethodCall(klass, method_call, depth);
  if(!lib_method) {
    LibraryClass* parent = linker->SearchClassLibraries(klass->GetParentName(), program->GetUses(current_class->GetFileName()));
    while(!lib_method && parent) {
      lib_method = ResolveMethodCall(parent, method_call, depth);
      parent = linker->SearchClassLibraries(parent->GetParentName(), program->GetUses(current_class->GetFileName()));
    }
  }

  // note: last resort to find system based methods i.e. $Int, $Float, etc.
  if(!lib_method) {
    wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() + L':' +
      encoding + EncodeMethodCall(method_call->GetCallingParameters(), depth);
    if(*encoded_name.rbegin() == L'*') {
      encoded_name.push_back(L',');
    }
    lib_method = klass->GetMethod(encoded_name);
  }

  // check private class scope
  const wstring bundle_name = klass->GetBundleName();
  if(!klass->IsPublic() && current_class && current_class->GetBundleName() != bundle_name) {
    ProcessError(static_cast<Expression*>(method_call), L"Cannot access private class '" + klass->GetName() + L"' from this bundle scope");
  }

  method_call->SetOriginalLibraryClass(klass);
  AnalyzeMethodCall(lib_method, method_call, klass->IsVirtual() && !is_parent, is_expr, depth);
}

/****************************
 * Analyzes a method call.  This
 * is method call within a linked
 * library
 ****************************/
void ContextAnalyzer::AnalyzeMethodCall(LibraryMethod* lib_method, MethodCall* method_call,
                                        bool is_virtual, bool is_expr, const int depth)
{
  if(lib_method) {
    ExpressionList* call_params = method_call->GetCallingParameters();
    vector<Expression*> expressions = call_params->GetExpressions();

    for(size_t i = 0; i < expressions.size(); ++i) {
      Expression* expression = expressions[i];
      if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
         expression->GetEvalType()->GetType() == NIL_TYPE) {
        ProcessError(static_cast<Expression*>(method_call), L"Invalid operation with 'Nil' value");
      }
    }

    // public/private check
    if(method_call->GetCallType() != NEW_INST_CALL && method_call->GetCallType() != PARENT_CALL && 
       !lib_method->IsStatic() && lib_method->GetLibraryClass() && !lib_method->GetLibraryClass()->GetParentName().empty()) {
      if(method_call->GetPreviousExpression()) {
        Expression* pre_expr = method_call->GetPreviousExpression();
        while(pre_expr->GetPreviousExpression()) {
          pre_expr = pre_expr->GetPreviousExpression();
        }
        switch(pre_expr->GetExpressionType()) {
        case METHOD_CALL_EXPR: {
          MethodCall* prev_method_call = static_cast<MethodCall*>(pre_expr);
          if(prev_method_call->GetCallType() != NEW_INST_CALL && prev_method_call->GetLibraryMethod() &&
             !prev_method_call->GetLibraryMethod()->IsStatic() && !prev_method_call->GetEntry() && 
             !prev_method_call->GetVariable()) {
            ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a method from this context");
          }
        }
          break;
        
        case CHAR_STR_EXPR:
        case STAT_ARY_EXPR:
        case VAR_EXPR:
          break;

        default:
          ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a method from this context");
          break;
        }
      }
      else if(!method_call->GetEntry() && !method_call->GetVariable()) {
        ProcessError(static_cast<Expression*>(method_call), L"Cannot reference a method from this context");
      }
    }

    // cannot create an instance of a virtual class
    if((lib_method->GetMethodType() == NEW_PUBLIC_METHOD ||
       lib_method->GetMethodType() == NEW_PRIVATE_METHOD) && is_virtual) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot create an instance of a virtual class or interface");
    }

    // associate method
    lib_method->GetLibraryClass()->SetCalled(true);
    method_call->SetLibraryMethod(lib_method);

    if(method_call->GetMethodCall()) {
      method_call->GetMethodCall()->SetEvalType(lib_method->GetReturn(), false);
    }

    // enum check
    if(method_call->GetMethodCall() && method_call->GetMethodCall()->GetCallType() == ENUM_CALL) {
      ProcessError(static_cast<Expression*>(method_call), L"Invalid enum reference");
    }

    if(lib_method->GetReturn()->GetType() == NIL_TYPE && method_call->GetCastType()) {
      ProcessError(static_cast<Expression*>(method_call), L"Cannot cast a Nil return value");
    }
    
    // map concrete to generic types
    LibraryClass* lib_klass = lib_method->GetLibraryClass();
    const bool is_new = lib_method->GetMethodType() == NEW_PUBLIC_METHOD || lib_method->GetMethodType() == NEW_PRIVATE_METHOD;
    const bool same_cls_return = ClassEquals(lib_method->GetReturn()->GetName(), nullptr, lib_klass);
    if((is_new || same_cls_return) && lib_klass->HasGenerics()) {
      const vector<LibraryClass*> class_generics = lib_klass->GetGenericClasses();
      const vector<Type*> concrete_types = GetConcreteTypes(method_call);
      if(class_generics.size() != concrete_types.size()) {
        ProcessError(static_cast<Expression*>(method_call), L"Cannot create an unqualified instance of class: '" + lib_method->GetUserName() + L"'");
      }
      // check individual types
      if(class_generics.size() == concrete_types.size()) {
        for(size_t i = 0; i < concrete_types.size(); ++i) {
          Type* concrete_type = concrete_types[i];
          LibraryClass* class_generic = class_generics[i];
          if(class_generic->HasGenericInterface()) {
            Type* backing_type = class_generic->GetGenericInterface();
            // backing type
            ResolveClassEnumType(backing_type);
            const wstring backing_name = backing_type->GetName();
            // concrete type
            ResolveClassEnumType(concrete_type);
            // validate backing
            ValidateGenericBacking(concrete_type, backing_name, static_cast<Expression*>(method_call));
          }
        }
      }
      method_call->GetEvalType()->SetGenerics(concrete_types);
    }

    // resolve generic to concrete, if needed
    Type* eval_type = method_call->GetEvalType();
    if(lib_method->GetLibraryClass()->HasGenerics()) {
      eval_type = ResolveGenericType(eval_type, method_call, nullptr, lib_klass, true);
      method_call->SetEvalType(eval_type, false);
    }
    else if(lib_method->GetReturn()->HasGenerics()) {
      const vector<Type*> concretate_types = method_call->GetConcreteTypes();
      const vector<Type*> generic_types = lib_method->GetReturn()->GetGenerics();
      if(concretate_types.size() == generic_types.size()) {
        for(size_t i = 0; i < concretate_types.size(); ++i) {
          Type* concretate_type = concretate_types[i];
          ResolveClassEnumType(concretate_type);

          Type* generic_type = generic_types[i];
          ResolveClassEnumType(generic_type);

          if(concretate_type->GetName() != generic_type->GetName()) {
            ProcessError(static_cast<Expression*>(method_call), L"Generic type mismatch for class '" + lib_method->GetLibraryClass()->GetName() +
                         L"' between generic types: '" + ReplaceSubstring(concretate_type->GetName(), L"#", L"->") +
                         L"' and '" + ReplaceSubstring(generic_type->GetName(), L"#", L"->") + L"'");
          }
        }
      }
      else {
        ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
      }
    }

    // next call
    AnalyzeExpressionMethodCall(method_call, depth + 1);
  }
  else {
    AnalyzeVariableFunctionCall(method_call, depth + 1);
  }
}

/********************************
 * Analyzes a dynamic function
 * call
 ********************************/
void ContextAnalyzer::AnalyzeVariableFunctionCall(MethodCall* method_call, const int depth)
{
  // dynamic function call that is not bound to a class/function until runtime
  SymbolEntry* entry = GetEntry(method_call->GetMethodName());
  if(entry && entry->GetType() && entry->GetType()->GetType() == FUNC_TYPE) {
    // generate parameter strings
    Type* type = entry->GetType();
    AnalyzeVariableFunctionParameters(type, static_cast<Expression*>(method_call));

    // get calling and function parameters
    const vector<Type*> func_params = type->GetFunctionParameters();
    vector<Expression*> calling_params = method_call->GetCallingParameters()->GetExpressions();
    if(func_params.size() != calling_params.size()) {
      ProcessError(static_cast<Expression*>(method_call), L"Function call parameter size mismatch");
      return;
    }

    // check parameters
    wstring dyn_func_params_str;
    ExpressionList* boxed_resolved_params = TreeFactory::Instance()->MakeExpressionList();
    for(size_t i = 0; i < func_params.size(); ++i) {
      Type* func_param = func_params[i];
      Expression* calling_param = calling_params[i];

      // check for boxing/unboxing
      Expression* boxed_param = BoxExpression(func_param, calling_param, depth + 1);
      if(boxed_param) {
        boxed_resolved_params->AddExpression(boxed_param);
      }
      else if((boxed_param = UnboxingExpression(func_param, calling_param, false, depth + 1))) {
        boxed_resolved_params->AddExpression(boxed_param);
      }
      // add default
      if(!boxed_param) {
        boxed_resolved_params->AddExpression(calling_param);
      }

      // encode parameter
      dyn_func_params_str += EncodeType(func_param);
      for(int j = 0; j < type->GetDimension(); ++j) {
        dyn_func_params_str += L'*';
      }
      dyn_func_params_str += L',';
    }
    
    // method call parameters
    type->SetFunctionParameterCount((int)method_call->GetCallingParameters()->GetExpressions().size());
    AnalyzeExpressions(boxed_resolved_params, depth + 1);

    // check parameters again dynamic definition
    const wstring call_params_str = EncodeMethodCall(boxed_resolved_params, depth);
    if(dyn_func_params_str != call_params_str) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" + method_call->GetMethodName() +
                   L"(..)'\n\tEnsure the object and it's calling parameters are properly casted");
    }
    // reset calling parameters
    method_call->SetCallingParameters(boxed_resolved_params);

    //  set entry reference and return type
    method_call->SetFunctionalCall(entry);
    method_call->SetEvalType(type->GetFunctionReturn(), true);
    if(method_call->GetMethodCall()) {
      method_call->GetMethodCall()->SetEvalType(type->GetFunctionReturn(), false);
    }

    // next call
    AnalyzeExpressionMethodCall(method_call, depth + 1);
  }
  else {
    const wstring &mthd_name = method_call->GetMethodName();
    const wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      wstring message = L"Undefined function/method call: '" + mthd_name +
        L"(..)'\n\tEnsure the object and it's calling parameters are properly casted";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
    else {
      wstring message = L"Undefined function/method call: '" + var_name +
        L"(..)'\n\tEnsure the object and it's calling parameters are properly casted";
      ProcessErrorAlternativeMethods(message);
      ProcessError(static_cast<Expression*>(method_call), message);
    }
  }
}

/********************************
 * Analyzes a function reference
 ********************************/
void ContextAnalyzer::AnalyzeFunctionReference(Class* klass, MethodCall* method_call,
                                               wstring &encoding, const int depth)
{
  const wstring func_encoding = EncodeFunctionReference(method_call->GetCallingParameters(), depth);
  const wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() +
    L':' + encoding + func_encoding;

  Method* method = klass->GetMethod(encoded_name);
  if(method) {
    const wstring func_type_id = L"m.(" + func_encoding + L")~" + method->GetEncodedReturn();
    
    Type* type = TypeParser::ParseType(func_type_id);
    type->SetFunctionParameterCount((int)method_call->GetCallingParameters()->GetExpressions().size());
    type->SetFunctionReturn(method->GetReturn());
    method_call->SetEvalType(type, true);

    if(!method->IsStatic()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods are not allowed, only functions");
    }

    if(method->IsVirtual()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods cannot be virtual");
    }

    // check return type
    Type* rtrn_type = method_call->GetFunctionalReturn();
    if(rtrn_type->GetType() != method->GetReturn()->GetType()) {
      ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
    }
    else if(rtrn_type->GetType() == CLASS_TYPE) {
      if(ResolveClassEnumType(rtrn_type)) {
        const wstring rtrn_encoded_name = L"o." + rtrn_type->GetName();
        if(rtrn_encoded_name != method->GetEncodedReturn()) {
          ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
        }
      }
      else {
        ProcessError(static_cast<Expression*>(method_call),
                     L"Undefined class or enum: '" + ReplaceSubstring(rtrn_type->GetName(), L"#", L"->") + L"'");
      }
    }
    method->GetClass()->SetCalled(true);
    method_call->SetOriginalClass(klass);
    method_call->SetMethod(method, false);
  }
  else {
    const wstring &mthd_name = method_call->GetMethodName();
    const wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   mthd_name + L"(..)'\n\tEnsure the object and it's calling parameters are properly casted");
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   var_name + L"(..)'\n\tEnsure the object and it's calling parameters are properly casted");
    }
  }
}

/****************************
 * Checks a function reference
 ****************************/
void ContextAnalyzer::AnalyzeFunctionReference(LibraryClass* klass, MethodCall* method_call,
                                               wstring &encoding, const int depth)
{
  const wstring func_encoding = EncodeFunctionReference(method_call->GetCallingParameters(), depth);
  const wstring encoded_name = klass->GetName() + L':' + method_call->GetMethodName() + L':' + encoding + func_encoding;

  LibraryMethod* method = klass->GetMethod(encoded_name);
  if(method) {
    const wstring func_type_id = L'(' + func_encoding + L")~" + method->GetEncodedReturn();
    Type* type = TypeParser::ParseType(func_type_id);
    type->SetFunctionParameterCount((int)method_call->GetCallingParameters()->GetExpressions().size());
    type->SetFunctionReturn(method->GetReturn());
    method_call->SetEvalType(type, true);

    if(!method->IsStatic()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods are not allowed, only functions");
    }

    if(method->IsVirtual()) {
      ProcessError(static_cast<Expression*>(method_call), L"References to methods cannot be virtual");
    }

    // check return type
    Type* rtrn_type = method_call->GetFunctionalReturn();
    if(rtrn_type->GetType() != method->GetReturn()->GetType()) {
      ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
    }
    else if(rtrn_type->GetType() == CLASS_TYPE) {
      if(ResolveClassEnumType(rtrn_type)) {
        const wstring rtrn_encoded_name = L"o." + rtrn_type->GetName();
        if(rtrn_encoded_name != method->GetEncodedReturn()) {
          ProcessError(static_cast<Expression*>(method_call), L"Mismatch function return types");
        }
      }
      else {
        ProcessError(static_cast<Expression*>(method_call),
                     L"Undefined class or enum: '" + ReplaceSubstring(rtrn_type->GetName(), L"#", L"->") + L"'");
      }
    }
    method->GetLibraryClass()->SetCalled(true);
    method_call->SetOriginalLibraryClass(klass);
    method_call->SetLibraryMethod(method, false);
  }
  else {
    const wstring &mthd_name = method_call->GetMethodName();
    const wstring &var_name = method_call->GetVariableName();

    if(mthd_name.size() > 0) {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   mthd_name + L"(..)'\n\tEnsure the object and it's calling parameters are properly casted");
    }
    else {
      ProcessError(static_cast<Expression*>(method_call), L"Undefined function/method call: '" +
                   var_name + L"(..)'\n\tEnsure the object and it's calling parameters are properly casted");
    }
  }
}

/****************************
 * Analyzes a cast
 ****************************/
void ContextAnalyzer::AnalyzeCast(Expression* expression, const int depth)
{
  // type cast
  if(expression->GetCastType()) {
    // get cast and root types
    Type* cast_type = expression->GetCastType();
    Type* root_type = expression->GetBaseType();
    if(!root_type) {
      root_type = expression->GetEvalType();
    }

    if(root_type && root_type->GetType() == VAR_TYPE) {
      ProcessError(expression, L"Cannot cast an uninitialized type");
    }

    // cannot cast across different dimensions
    if(root_type && expression->GetExpressionType() == VAR_EXPR &&
       !static_cast<Variable*>(expression)->GetIndices() &&
       cast_type->GetDimension() != root_type->GetDimension()) {
      ProcessError(expression, L"Dimension size mismatch");
    }

    AnalyzeRightCast(cast_type, root_type, expression, IsScalar(expression), depth + 1);
  }
  // typeof check
  else if(expression->GetTypeOf()) {
    if(expression->GetTypeOf()->GetType() != CLASS_TYPE ||
      (expression->GetEvalType() && expression->GetEvalType()->GetType() != CLASS_TYPE)) {
      ProcessError(expression, L"Invalid 'TypeOf' check, only complex classes are supported");
    }

    Type* type_of = expression->GetTypeOf();
    if(SearchProgramClasses(type_of->GetName())) {
      Class* klass = SearchProgramClasses(type_of->GetName());
      klass->SetCalled(true);
      type_of->SetName(klass->GetName());
    }
    else if(linker->SearchClassLibraries(type_of->GetName(), program->GetUses(current_class->GetFileName()))) {
      LibraryClass* lib_klass = linker->SearchClassLibraries(type_of->GetName(), program->GetUses(current_class->GetFileName()));
      lib_klass->SetCalled(true);
      type_of->SetName(lib_klass->GetName());
    }
    else {
      ProcessError(expression, L"Invalid 'TypeOf' check, unknown class '" + type_of->GetName() + L"'");
    }
    expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
  }
}

/****************************
 * Analyzes array indices
 ****************************/
void ContextAnalyzer::AnalyzeIndices(ExpressionList* indices, const int depth)
{
  AnalyzeExpressions(indices, depth + 1);

  vector<Expression*> expressions = indices->GetExpressions();
  for(size_t i = 0; i < expressions.size(); ++i) {
    Expression* expression = expressions[i];
    AnalyzeExpression(expression, depth + 1);
    Type* eval_type = expression->GetEvalType();
    if(eval_type) {
      switch(eval_type->GetType()) {
      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        break;

      case CLASS_TYPE:
        if(!IsEnumExpression(expression)) {
          Expression* unboxed_expresion = UnboxingExpression(eval_type, expression, true, depth);
          if(unboxed_expresion) {
            expressions.push_back(unboxed_expresion);
          }
          else {
            ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
          }
        }
        break;

      default:
        ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
        break;
      }
    }
  }
}

/****************************
 * Analyzes a simple statement
 ****************************/
void ContextAnalyzer::AnalyzeSimpleStatement(SimpleStatement* simple, const int depth)
{
  Expression* expression = simple->GetExpression();
  AnalyzeExpression(expression, depth + 1);
  AnalyzeExpressionMethodCall(expression, depth);

  // ensure it's a valid statement
  if(!expression->GetMethodCall()) {
    ProcessError(expression, L"Invalid statement");
  }
}

/****************************
 * Analyzes a 'if' statement
 ****************************/
void ContextAnalyzer::AnalyzeIf(If* if_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"if/else-if/else", if_stmt->GetLineNumber(), depth);
#endif

  // expression
  Expression* expression = if_stmt->GetExpression();
  AnalyzeExpression(expression, depth + 1);
  if(!IsBooleanExpression(expression)) {
    ProcessError(expression, L"Expected Bool expression");
  }
  // 'if' statements
  AnalyzeStatements(if_stmt->GetIfStatements(), depth + 1);

  If* next = if_stmt->GetNext();
  if(next) {
    AnalyzeIf(next, depth);
  }

  // 'else'
  StatementList* else_list = if_stmt->GetElseStatements();
  if(else_list) {
    AnalyzeStatements(else_list, depth + 1);
  }
}

/****************************
 * Analyzes a 'select' statement
 ****************************/
void ContextAnalyzer::AnalyzeSelect(Select* select_stmt, const int depth)
{
  // expression
  Expression* expression = select_stmt->GetAssignment()->GetExpression();
  AnalyzeExpression(expression, depth + 1);
  if(!IsIntegerExpression(expression)) {
    ProcessError(expression, L"Expected integer expression");
  }
  // labels and expressions
  map<ExpressionList*, StatementList*> statements = select_stmt->GetStatements();
  if(statements.size() < 1) {
    ProcessError(expression, L"Select statement must have at least one label");
  }

  map<ExpressionList*, StatementList*>::iterator iter;
  // duplicate value vector
  int value = 0;
  map<int, StatementList*> label_statements;
  for(iter = statements.begin(); iter != statements.end(); ++iter) {
    // expressions
    ExpressionList* expressions = iter->first;
    AnalyzeExpressions(expressions, depth + 1);
    // check expression type
    vector<Expression*> expression_list = expressions->GetExpressions();
    for(size_t i = 0; i < expression_list.size(); ++i) {
      Expression* expression = expression_list[i];
      switch(expression->GetExpressionType()) {
      case CHAR_LIT_EXPR:
        value = static_cast<CharacterLiteral*>(expression)->GetValue();
        if(DuplicateCaseItem(label_statements, value)) {
          ProcessError(expression, L"Duplicate select value");
        }
        break;

      case INT_LIT_EXPR:
        value = static_cast<IntegerLiteral*>(expression)->GetValue();
        if(DuplicateCaseItem(label_statements, value)) {
          ProcessError(expression, L"Duplicate select value");
        }
        break;

      case METHOD_CALL_EXPR: {
        // get method call
        MethodCall* mthd_call = static_cast<MethodCall*>(expression);
        if(mthd_call->GetMethodCall()) {
          mthd_call = mthd_call->GetMethodCall();
        }
        // check type
        if(mthd_call->GetEnumItem()) {
          value = mthd_call->GetEnumItem()->GetId();
          if(DuplicateCaseItem(label_statements, value)) {
            ProcessError(expression, L"Duplicate select value");
          }
        }
        else if(mthd_call->GetLibraryEnumItem()) {
          value = mthd_call->GetLibraryEnumItem()->GetId();
          if(DuplicateCaseItem(label_statements, value)) {
            ProcessError(expression, L"Duplicate select value");
          }
        }
        else {
          ProcessError(expression, L"Expected integer literal or enum item");
        }
      }
        break;
        
      default:
        ProcessError(expression, L"Expected integer literal or enum item");
        break;
      }
      // statements get assoicated here and validated below
      label_statements.insert(pair<int, StatementList*>(value, iter->second));
    }
  }
  select_stmt->SetLabelStatements(label_statements);

  // process statements (in parse order)
  vector<StatementList*> statement_lists = select_stmt->GetStatementLists();
  for(size_t i = 0; i < statement_lists.size(); ++i) {
    AnalyzeStatements(statement_lists[i], depth + 1);
  }
}

/****************************
 * Analyzes a 'for' statement
 ****************************/
void ContextAnalyzer::AnalyzeCritical(CriticalSection* mutex, const int depth)
{
  Variable* variable = mutex->GetVariable();
  AnalyzeVariable(variable, depth + 1);
  if(variable->GetEvalType() && variable->GetEvalType()->GetType() == CLASS_TYPE) {
    if(variable->GetEvalType()->GetName() != L"System.Concurrency.ThreadMutex") {
      ProcessError(mutex, L"Expected ThreadMutex type");
    }
  }
  else {
    ProcessError(mutex, L"Expected ThreadMutex type");
  }
  AnalyzeStatements(mutex->GetStatements(), depth + 1);
}

/****************************
 * Analyzes a 'for' statement
 ****************************/
void ContextAnalyzer::AnalyzeFor(For* for_stmt, const int depth)
{
  current_table->NewScope();
  // pre
  AnalyzeStatement(for_stmt->GetPreStatement(), depth + 1);
  // expression
  Expression* expression = for_stmt->GetExpression();
  AnalyzeExpression(expression, depth + 1);
  if(!IsBooleanExpression(expression)) {
    ProcessError(expression, L"Expected Bool expression");
  }
  // update
  AnalyzeStatement(for_stmt->GetUpdateStatement(), depth + 1);
  // statements
  in_loop++;
  AnalyzeStatements(for_stmt->GetStatements(), depth + 1);
  in_loop--;
  current_table->PreviousScope();
}

/****************************
 * Analyzes a 'do/while' statement
 ****************************/
void ContextAnalyzer::AnalyzeDoWhile(DoWhile* do_while_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"do/while", do_while_stmt->GetLineNumber(), depth);
#endif

  // 'do/while' statements
  current_table->NewScope();
  in_loop++;
  vector<Statement*> statements = do_while_stmt->GetStatements()->GetStatements();
  for(size_t i = 0; i < statements.size(); ++i) {
    AnalyzeStatement(statements[i], depth + 2);
  }
  in_loop--;

  // expression
  Expression* expression = do_while_stmt->GetExpression();
  AnalyzeExpression(expression, depth + 1);
  if(!IsBooleanExpression(expression)) {
    ProcessError(expression, L"Expected Bool expression");
  }
  current_table->PreviousScope();
}

/****************************
 * Analyzes a 'while' statement
 ****************************/
void ContextAnalyzer::AnalyzeWhile(While* while_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"while", while_stmt->GetLineNumber(), depth);
#endif

  // expression
  Expression* expression = while_stmt->GetExpression();
  AnalyzeExpression(expression, depth + 1);
  if(!IsBooleanExpression(expression)) {
    ProcessError(expression, L"Expected Bool expression");
  }
  // 'while' statements
  in_loop++;
  AnalyzeStatements(while_stmt->GetStatements(), depth + 1);
  in_loop--;
}

/****************************
 * Analyzes a return statement
 ****************************/
void ContextAnalyzer::AnalyzeReturn(Return* rtrn, const int depth)
{
#ifdef _DEBUG
  Debug(L"return", rtrn->GetLineNumber(), depth);
#endif

  Type* mthd_type = current_method->GetReturn();
  Expression* expression = rtrn->GetExpression();
  if(expression) {
    AnalyzeExpression(expression, depth + 1);
    while(expression->GetMethodCall()) {
      AnalyzeExpressionMethodCall(expression, depth + 1);
      expression = expression->GetMethodCall();
    }

    bool is_nil_lambda_expr = false;
    if(expression->GetExpressionType() == METHOD_CALL_EXPR && expression->GetEvalType() &&
       expression->GetEvalType()->GetType() == NIL_TYPE) {
      if(capture_lambda) {
        is_nil_lambda_expr = true;
      }
      else {
        ProcessError(expression, L"Invalid operation with 'Nil' value");
      }
    }

    MethodCall* boxed_call = BoxUnboxingReturn(mthd_type, expression, depth);
    if(boxed_call) {
      AnalyzeExpression(boxed_call, depth + 1);
      rtrn->SetExpression(boxed_call);
      expression = boxed_call;
    }
    
    if(is_nil_lambda_expr && expression->GetExpressionType() == METHOD_CALL_EXPR) {
      MethodCall* mthd_call = static_cast<MethodCall*>(expression);
      if(mthd_call->GetMethod()) {
        if(mthd_call->GetMethod()->GetReturn()->GetType() == NIL_TYPE && mthd_type->GetType() != NIL_TYPE) {
          ProcessError(rtrn, L"Method call returns no value, value expected");
        }
      }
      else if(mthd_call->GetLibraryMethod()) {
        if(mthd_call->GetLibraryMethod()->GetReturn()->GetType() == NIL_TYPE && mthd_type->GetType() != NIL_TYPE) {
          ProcessError(rtrn, L"Method call returns no value, value expected");
        }
      }
    }
    else {
      Expression* box_expression = AnalyzeRightCast(mthd_type, expression, (IsScalar(expression) && mthd_type->GetDimension() == 0), depth + 1);
      if(box_expression) {
        AnalyzeExpression(box_expression, depth + 1);
        rtrn->SetExpression(box_expression);
        expression = box_expression;
      }
    }

    ValidateConcrete(expression->GetEvalType(), mthd_type, expression, depth);

    if(mthd_type->GetType() == CLASS_TYPE && !ResolveClassEnumType(mthd_type)) {
      ProcessError(rtrn, L"Undefined class or enum: '" + ReplaceSubstring(mthd_type->GetName(), L"#", L"->") + L"'");
    }
  }
  else if(mthd_type->GetType() != NIL_TYPE) {
    ProcessError(rtrn, L"Invalid return statement");
  }

  if(current_method->GetMethodType() == NEW_PUBLIC_METHOD ||
     current_method->GetMethodType() == NEW_PRIVATE_METHOD) {
    ProcessError(rtrn, L"Cannot return value from constructor");
  }
}

void ContextAnalyzer::ValidateConcrete(Type* cls_type, Type* concrete_type, ParseNode* node, const int depth)
{
  if(!cls_type || !concrete_type) {
    return;
  }

  const wstring concrete_type_name = concrete_type->GetName();
  Class* concrete_klass = nullptr; LibraryClass* concrete_lib_klass = nullptr;
  if(!GetProgramLibraryClass(concrete_type, concrete_klass, concrete_lib_klass)) {
     concrete_klass = current_class->GetGenericClass(concrete_type_name);
  }

  if(concrete_klass || concrete_lib_klass) {
    const bool is_concrete_inf = ((concrete_klass && concrete_klass->IsInterface()) ||
      (concrete_lib_klass && concrete_lib_klass->IsInterface())) ? true : false;

    if(!is_concrete_inf) {
      const wstring cls_type_name = cls_type->GetName();
      Class* dclr_klass = nullptr; LibraryClass* dclr_lib_klass = nullptr;
      if(!GetProgramLibraryClass(cls_type, dclr_klass, dclr_lib_klass)) {
        dclr_klass = current_class->GetGenericClass(cls_type_name);
      }

      if(dclr_klass && dclr_klass->HasGenerics()) {
        const vector<Type*> concrete_types = concrete_type->GetGenerics();
        if(concrete_types.empty()) {
          ProcessError(node, L"Generic to concrete size mismatch");
        }
        else {
          ValidateGenericConcreteMapping(concrete_types, dclr_klass, node);
        }
      }
      else if(dclr_lib_klass && dclr_lib_klass->HasGenerics()) {
        const vector<Type*> concrete_types = concrete_type->GetGenerics();
        if(concrete_types.empty()) {
          ProcessError(node, L"Generic to concrete size mismatch");
        }
        else {
          ValidateGenericConcreteMapping(concrete_types, dclr_lib_klass, node);
        }
      }
    }
  }
}

/****************************
 * Analyzes a return statement
 ****************************/
void ContextAnalyzer::AnalyzeLeaving(Leaving* leaving_stmt, const int depth)
{
#ifdef _DEBUG
  Debug(L"leaving", leaving_stmt->GetLineNumber(), depth);
#endif

  const int level = current_table->GetDepth();
  if(level == 1) {
    AnalyzeStatements(leaving_stmt->GetStatements(), depth + 1);
    if(current_method->GetLeaving()) {
      ProcessError(leaving_stmt, L"Method/function may have only 1 'leaving' block defined");
    }
    else {
      current_method->SetLeaving(leaving_stmt);
    }
  }
  else {
    ProcessError(leaving_stmt, L"Method/function 'leaving' block must be a top level statement");
  }
}

/****************************
 * Analyzes an assignment statement
 ****************************/
void ContextAnalyzer::AnalyzeAssignment(Assignment* assignment, StatementType type, const int depth)
{
#ifdef _DEBUG
  Debug(L"assignment", assignment->GetLineNumber(), depth);
#endif

  Variable* variable = assignment->GetVariable();
  AnalyzeVariable(variable, depth + 1);

  // get last expression for assignment
  Expression* expression = assignment->GetExpression();
  AnalyzeExpression(expression, depth + 1);
  if(expression->GetExpressionType() == LAMBDA_EXPR) {
    expression = static_cast<Lambda*>(expression)->GetMethodCall();
    if(!expression) {
      return;
    }
  }

  while(expression->GetMethodCall()) {
    AnalyzeExpressionMethodCall(expression, depth + 1);
    expression = expression->GetMethodCall();
  }

  // if uninitialized variable, bind and update entry
  if(variable->GetEvalType() && variable->GetEvalType()->GetType() == VAR_TYPE) {
    if(variable->GetIndices()) {
      ProcessError(expression, L"Invalid operation using Var type");
    }

    SymbolEntry* entry = variable->GetEntry();
    if(entry) {
      if(expression->GetCastType()) {
        Type* to_type = expression->GetCastType();
        AnalyzeVariableCast(to_type, expression);
        variable->SetTypes(to_type);
        entry->SetType(to_type);
      }
      else {
        Type* to_type = expression->GetEvalType();
        AnalyzeVariableCast(to_type, expression);
        variable->SetTypes(to_type);
        entry->SetType(to_type);
      }
      // set variable to scalar type if we're dereferencing an array variable
      if(expression->GetExpressionType() == VAR_EXPR) {
        Variable* expr_variable = static_cast<Variable*>(expression);
        if(entry->GetType() && expr_variable->GetIndices()) {
          variable->GetBaseType()->SetDimension(0);
          variable->GetEvalType()->SetDimension(0);
          entry->GetType()->SetDimension(0);
        }
      }
    }
  }
  // handle enum reference, update entry
  else if(variable->GetEvalType() && variable->GetEvalType()->GetType() == CLASS_TYPE && 
          expression->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(expression)->GetEnumItem()) {
    SymbolEntry* to_entry = variable->GetEntry();
    if(to_entry) {
      Type* to_type = to_entry->GetType();
      Expression* box_expression = BoxExpression(to_type, expression, depth);
      if(box_expression) {
        expression = box_expression;
        assignment->SetExpression(box_expression);
      }
      else {
        Type* from_type = expression->GetEvalType();
        AnalyzeClassCast(to_type, from_type, expression, false, depth);
        variable->SetTypes(from_type);
        to_entry->SetType(from_type);
      }
    }
  }
  
  // handle generics, update entry
  if(expression->GetEvalType() && expression->GetEvalType()->HasGenerics() && variable->GetEntry() && variable->GetEntry()->GetType()) {
    const vector<Type*> var_types = variable->GetEntry()->GetType()->GetGenerics();
    const vector <Type*> expr_types = expression->GetEvalType()->GetGenerics();
    
    if(var_types.size() == expr_types.size()) {
      for(size_t i = 0; i < var_types.size(); ++i) {
        // resolve variable type
        Type* var_type = var_types[i];
        ResolveClassEnumType(var_type);
        // resolve expression type
        Type* expr_type = expr_types[i];
        ResolveClassEnumType(expr_type);
        // match expression types
        if(var_type->GetName() != expr_type->GetName()) {
          ProcessError(variable, L"Generic type mismatch for class '" + variable->GetEvalType()->GetName() + 
                       L"' between generic types: '" + ReplaceSubstring(var_type->GetName(), L"#", L"->") + 
                       L"' and '" + ReplaceSubstring(expr_type->GetName(), L"#", L"->") + L"'");
        }
      }
    }
    else {
      ProcessError(variable, L"Generic size mismatch");
    }
  }
  
  Type* left_type = variable->GetEvalType();
  bool check_right_cast = true;
  if(left_type && left_type->GetType() == CLASS_TYPE) {
#ifndef _SYSTEM
    LibraryClass* left_class = linker->SearchClassLibraries(left_type->GetName(),program->GetUses(current_class->GetFileName()));
#else
    Class* left_class = SearchProgramClasses(left_type->GetName());
#endif
    if(left_class) {
      const wstring left_name = left_class->GetName();
      //
      // 'System.String' append operations
      //
      if(left_name == L"System.String") {
        
        Type* right_type = GetExpressionType(expression, depth + 1);
        if(right_type && right_type->GetType() == CLASS_TYPE) {
#ifndef _SYSTEM
          LibraryClass* right_class = linker->SearchClassLibraries(right_type->GetName(), program->GetUses(current_class->GetFileName()));
#else
          Class* right_class = SearchProgramClasses(right_type->GetName());
#endif
          if(right_class) {
            const wstring right = right_class->GetName();
            // rhs string append
            if(right == L"System.String") {
              switch(type) {
              case ADD_ASSIGN_STMT:
                static_cast<OperationAssignment*>(assignment)->SetStringConcat(true);
                check_right_cast = false;
                break;

              case SUB_ASSIGN_STMT:
              case MUL_ASSIGN_STMT:
              case DIV_ASSIGN_STMT:
                ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.String'");
                break;

              case ASSIGN_STMT:
                break;

              default:
                ProcessError(assignment, L"Internal compiler error.");
                exit(1);
              }
            }
            else {
              ProcessError(assignment, L"Invalid operation using classes: 'System.String' and '" + right + L"'");
            }
          }
          else {
            ProcessError(assignment, L"Invalid operation using classes: 'System.String' and '" + right_type->GetName() + L"'");
          }
        }
        // rhs 'Char', 'Byte', 'Int', 'Float' or 'Bool'
        else if(right_type && (right_type->GetType() == CHAR_TYPE || right_type->GetType() == BYTE_TYPE ||
                right_type->GetType() == INT_TYPE || right_type->GetType() == FLOAT_TYPE ||
                right_type->GetType() == BOOLEAN_TYPE)) {
          switch(type) {
          case ADD_ASSIGN_STMT:
            static_cast<OperationAssignment*>(assignment)->SetStringConcat(true);
            check_right_cast = false;
            break;

          case SUB_ASSIGN_STMT:
          case MUL_ASSIGN_STMT:
          case DIV_ASSIGN_STMT:
            if(right_type->GetType() == CHAR_TYPE) {
              ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Char'");
            }
            else if(right_type->GetType() == BYTE_TYPE) {
              ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Byte'");
            }
            else if(right_type->GetType() == INT_TYPE) {
              ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Int'");
            }
            else if(right_type->GetType() == FLOAT_TYPE) {
              ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Float'");
            }
            else {
              ProcessError(assignment, L"Invalid operation using classes: 'System.String' and 'System.Bool'");
            }
            break;

          case ASSIGN_STMT:
            break;

          default:
            ProcessError(assignment, L"Internal compiler error.");
            exit(1);
          }
        }
      }
      //
      // Unboxing for assignment operations
      //
      else if(IsHolderType(left_name)) {
        CalculatedExpression* calc_expression = nullptr;
        switch(type) {
        case ADD_ASSIGN_STMT:
          calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(),
                                                                              variable->GetLineNumber(),
                                                                              ADD_EXPR, variable, expression);
          break;

        case SUB_ASSIGN_STMT:
          calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(),
                                                                              variable->GetLineNumber(),
                                                                              SUB_EXPR, variable, expression);
          break;

        case MUL_ASSIGN_STMT:
          calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(),
                                                                              variable->GetLineNumber(),
                                                                              MUL_EXPR, variable, expression);
          break;

        case DIV_ASSIGN_STMT:
          calc_expression = TreeFactory::Instance()->MakeCalculatedExpression(variable->GetFileName(), 
                                                                              variable->GetLineNumber(),
                                                                              DIV_EXPR, variable, expression);
          break;

        case ASSIGN_STMT:
          break;

        default:
          ProcessError(assignment, L"Internal compiler error.");
          exit(1);
        }

        if(calc_expression) {
          assignment->SetExpression(calc_expression);
          expression = calc_expression;
          static_cast<OperationAssignment*>(assignment)->SetStatementType(ASSIGN_STMT);
          AnalyzeCalculation(calc_expression, depth + 1);
        }
      }
    }
  }

  if(check_right_cast) {
    Expression* box_expression = AnalyzeRightCast(variable, expression, (IsScalar(variable) && IsScalar(expression)), depth + 1);
    if(box_expression) {
      AnalyzeExpression(box_expression, depth + 1);
      assignment->SetExpression(box_expression);
    }
  }

  if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* method_call = static_cast<MethodCall*>(expression);
    // 'Nil' return check
    if(method_call->GetMethod() && method_call->GetMethod()->GetReturn()->GetType() == NIL_TYPE &&
       !method_call->IsFunctionDefinition()) {
      ProcessError(expression, L"Invalid assignment method '" + method_call->GetMethod()->GetName() + L"(..)' does not return a value");
    }
    else if(method_call->GetEvalType() && method_call->GetEvalType()->GetType() == NIL_TYPE) {
      ProcessError(expression, L"Invalid assignment, call does not return a value");
    }
  }
}

/****************************
 * Analyzes a logical or mathematical
 * operation.
 ****************************/
void ContextAnalyzer::AnalyzeCalculation(CalculatedExpression* expression, const int depth)
{
  Type* cls_type = nullptr;
  Expression* left = expression->GetLeft();
  switch(left->GetExpressionType()) {
  case AND_EXPR:
  case OR_EXPR:
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EXPR:
  case LES_EQL_EXPR:
  case GTR_EQL_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
  case SHL_EXPR:
  case SHR_EXPR:
  case BIT_AND_EXPR:
  case BIT_OR_EXPR:
  case BIT_XOR_EXPR:
    AnalyzeCalculation(static_cast<CalculatedExpression*>(left), depth + 1);
    break;

  default:
    break;
  }

  Expression* right = expression->GetRight();
  switch(right->GetExpressionType()) {
  case AND_EXPR:
  case OR_EXPR:
  case EQL_EXPR:
  case NEQL_EXPR:
  case LES_EXPR:
  case GTR_EXPR:
  case LES_EQL_EXPR:
  case GTR_EQL_EXPR:
  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case MOD_EXPR:
  case SHL_EXPR:
  case SHR_EXPR:
  case BIT_AND_EXPR:
  case BIT_OR_EXPR:
  case BIT_XOR_EXPR:
    AnalyzeCalculation(static_cast<CalculatedExpression*>(right), depth + 1);
    break;

  default:
    break;
  }
  AnalyzeExpression(left, depth + 1);
  AnalyzeExpression(right, depth + 1);

  // check operations
  AnalyzeCalculationCast(expression, depth);

  // check for valid operation cast
  if(left->GetCastType() && left->GetEvalType()) {
    AnalyzeRightCast(left->GetCastType(), left->GetEvalType(), left, IsScalar(left), depth);
  }

  // check for valid operation cast
  if(right->GetCastType() && right->GetEvalType()) {
    AnalyzeRightCast(right->GetCastType(), right->GetEvalType(), right, IsScalar(right), depth);
  }

  switch(expression->GetExpressionType()) {
  case AND_EXPR:
  case OR_EXPR:
    if(!IsBooleanExpression(left) || !IsBooleanExpression(right)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    break;

  case EQL_EXPR:
  case NEQL_EXPR:
    if(IsBooleanExpression(left) && !IsBooleanExpression(right)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    else if(!IsBooleanExpression(left) && IsBooleanExpression(right)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
    break;

  case LES_EXPR:
  case GTR_EXPR:
  case LES_EQL_EXPR:
  case GTR_EQL_EXPR:
    if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    else if(IsEnumExpression(left) && IsEnumExpression(right)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    else if(((cls_type = GetExpressionType(left, depth + 1)) && cls_type->GetType() == CLASS_TYPE) ||
      ((cls_type = GetExpressionType(right, depth + 1)) && cls_type->GetType() == CLASS_TYPE)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    else if((left->GetEvalType() && left->GetEvalType()->GetType() == NIL_TYPE) ||
      (right->GetEvalType() && right->GetEvalType()->GetType() == NIL_TYPE)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    expression->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
    break;

  case MOD_EXPR:
    if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    else if(((cls_type = GetExpressionType(left, depth + 1)) && cls_type->GetType() == CLASS_TYPE) || ((cls_type = GetExpressionType(right, depth + 1)) && cls_type->GetType() == CLASS_TYPE)) {
      const wstring cls_name = cls_type->GetName();
      if(cls_name != L"System.ByteHolder" && cls_name != L"System.CharHolder" && cls_name != L"System.IntHolder") {
        ProcessError(expression, L"Invalid mathematical operation");
      }
    }

    if(left->GetEvalType() && GetExpressionType(left, depth + 1)->GetType() == FLOAT_TYPE) {
      if(left->GetCastType()) {
        switch(left->GetCastType()->GetType()) {
        case BYTE_TYPE:
        case INT_TYPE:
        case CHAR_TYPE:
          break;
        default:
          ProcessError(expression, L"Expected Byte, Char, Int or Enum class type");
          break;
        }
      }
      else {
        ProcessError(expression, L"Expected Byte, Char, Int Enum class type");
      }
    }

    if(right->GetEvalType() && GetExpressionType(right, depth + 1)->GetType() == FLOAT_TYPE) {
      if(right->GetCastType()) {
        switch(right->GetCastType()->GetType()) {
        case BYTE_TYPE:
        case INT_TYPE:
        case CHAR_TYPE:
          break;
        default:
          ProcessError(expression, L"Expected Byte, Char, Int Enum class type");
          break;
        }
      }
      else {
        ProcessError(expression, L"Expected Byte, Char, Int Enum class type");
      }
    }
    break;

  case ADD_EXPR:
  case SUB_EXPR:
  case MUL_EXPR:
  case DIV_EXPR:
  case SHL_EXPR:
  case SHR_EXPR:
  case BIT_AND_EXPR:
  case BIT_OR_EXPR:
  case BIT_XOR_EXPR:
    if(IsBooleanExpression(left) || IsBooleanExpression(right)) {
      ProcessError(expression, L"Invalid mathematical operation");
    }
    break;

  default:
    break;
  }
}

/****************************
 * Preforms type conversions
 * operational expressions.  This
 * method uses execution simulation.
 ****************************/
void ContextAnalyzer::AnalyzeCalculationCast(CalculatedExpression* expression, const int depth)
{
  Expression* left_expr = expression->GetLeft();
  Expression* right_expr = expression->GetRight();

  Type* left = GetExpressionType(left_expr, depth + 1);
  Type* right = GetExpressionType(right_expr, depth + 1);

  if(!left || !right) {
    return;
  }

  if(!IsScalar(left_expr) || !IsScalar(right_expr)) {
    if(right->GetType() != NIL_TYPE) {
      ProcessError(left_expr, L"Invalid array calculation");
    }
  }
  else {
    switch(left->GetType()) {
    case VAR_TYPE:
      // VAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Function");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Float");
        break;

      case CLASS_TYPE:
        if(HasProgramLibraryEnum(right->GetName())) {
          ProcessError(left_expr, L"Invalid operation using classes: Var and Enum");
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Var and System.Bool");
        break;
      }
      break;
        
    case ALIAS_TYPE:
      break;

    case NIL_TYPE:
      // NIL
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Float");
        break;

      case CLASS_TYPE:
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: Nil and System.Bool");
        break;
      }
      break;

    case BYTE_TYPE:
      // BYTE
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and Var");
        break;
      
      case ALIAS_TYPE:
        break;
          
      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and Nil");
        break;

      case CHAR_TYPE:
      case INT_TYPE:
      case BYTE_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_expr->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Int and " +
                       ReplaceSubstring(right->GetName(), L"#", L"->"));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Byte and System.Bool");
        break;
      }
      break;

    case CHAR_TYPE:
      // CHAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Char and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Char and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Char and Nil");
        break;

      case INT_TYPE:
      case CHAR_TYPE:
      case BYTE_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_expr->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Int and " +
                       ReplaceSubstring(right->GetName(), L"#", L"->"));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes:  Char and System.Bool");
        break;
      }
      break;

    case INT_TYPE:
      // INT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and Nil");
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        expression->SetEvalType(left, true);
        break;

      case FLOAT_TYPE:
        left_expr->SetCastType(right, true);
        expression->SetEvalType(right, true);
        break;

      case CLASS_TYPE:
        if(HasProgramLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Int and " +
                       ReplaceSubstring(right->GetName(), L"#", L"->"));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Int and System.Bool");
        break;
      }
      break;

    case FLOAT_TYPE:
      // FLOAT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and Nil");
        break;

      case FLOAT_TYPE:
        expression->SetEvalType(left, true);
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        right_expr->SetCastType(left, true);
        expression->SetEvalType(left, true);
        break;

      case CLASS_TYPE:
        if(HasProgramLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(UnboxingCalculation(right, right_expr, expression, false, depth)) {
          expression->SetEvalType(left, true);
        }
        else {
          ProcessError(left_expr, L"Invalid operation using classes: System.Float and " +
                       ReplaceSubstring(right->GetName(), L"#", L"->"));
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Float and System.Bool");
        break;
      }
      break;

    case CLASS_TYPE:
      // CLASS
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: " +
                     ReplaceSubstring(left->GetName(), L"#", L"->") +
                     L" and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: " +
                     ReplaceSubstring(left->GetName(), L"#", L"->") +
                     L" and Var");
        break;

      case ALIAS_TYPE:
      case NIL_TYPE:
        break;

      case BYTE_TYPE:
        if(HasProgramLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " +
                       ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Byte");
        }
        break;

      case CHAR_TYPE:
        if(HasProgramLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " +
                       ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Char");
        }
        break;

      case INT_TYPE:
        if(HasProgramLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        }
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " +
                       ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Int");
        }
        break;

      case FLOAT_TYPE:
        if(HasProgramLibraryEnum(left->GetName())) {
          left_expr->SetCastType(right, true);
          expression->SetEvalType(right, true);
        } 
        else if(!UnboxingCalculation(left, left_expr, expression, true, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: " +
                       ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Float");
        }
        break;

      case CLASS_TYPE: {
        ResolveClassEnumType(left);
        const bool can_unbox_left = UnboxingCalculation(left, left_expr, expression, true, depth);
        const bool is_left_enum = HasProgramLibraryEnum(left->GetName());

        ResolveClassEnumType(right);
        const bool can_unbox_right = UnboxingCalculation(right, right_expr, expression, false, depth);
        const bool is_right_enum = HasProgramLibraryEnum(right->GetName());
        
        if((is_left_enum && is_right_enum) || (can_unbox_left && can_unbox_right)) {
          AnalyzeClassCast(left, right, left_expr, false, depth + 1);
        }
        else if(can_unbox_left && !is_right_enum) {
          ProcessError(left_expr, L"Invalid operation between class and enum: '" + left->GetName() + L"' and '" + right->GetName() + L"'");
        }
        else if(can_unbox_right && !is_left_enum) {
          ProcessError(left_expr, L"Invalid operation between class and enum: '" + left->GetName() + L"' and '" + right->GetName() + L"'");
        }
        else if(((!can_unbox_left && !is_left_enum) || (!can_unbox_right && !is_right_enum)) &&
                expression->GetExpressionType() != EQL_EXPR && expression->GetExpressionType() != NEQL_EXPR) {
          ProcessError(left_expr, L"Invalid operation between class or enum: '" + left->GetName() + L"' and '" + right->GetName() + L"'");
        }
        if(left->GetName() == L"System.FloatHolder" || right->GetName() == L"System.FloatHolder") {
          expression->SetEvalType(TypeFactory::Instance()->MakeType(FLOAT_TYPE), true);
        }
        else {
          expression->SetEvalType(TypeFactory::Instance()->MakeType(INT_TYPE), true);
        }
      }
        break;
        
      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: " +
                     ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Bool");
        break;
      }
      break;

    case BOOLEAN_TYPE:
      // BOOLEAN
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and function reference");
        break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: System.Bool and System.Float");
        break;

      case CLASS_TYPE:
        if(HasProgramLibraryEnum(right->GetName())) {
          right_expr->SetCastType(left, true);
          expression->SetEvalType(left, true);
        }
        else if(!UnboxingCalculation(right, right_expr, expression, false, depth)) {
          ProcessError(left_expr, L"Invalid operation using classes: System.Bool and " +
                       ReplaceSubstring(right->GetName(), L"#", L"->"));
        }
        break;
        
      case BOOLEAN_TYPE:
        expression->SetEvalType(left, true);
        break;
      }
      break;

    case FUNC_TYPE:
      // FUNCTION
      switch(right->GetType()) {
      case FUNC_TYPE: {
        AnalyzeVariableFunctionParameters(left, expression);
        if(left->GetName().size() == 0) {
          left->SetName(L"m." + EncodeFunctionType(left->GetFunctionParameters(),
                             left->GetFunctionReturn()));
        }

        if(right->GetName().size() == 0) {
          right->SetName(L"m." + EncodeFunctionType(right->GetFunctionParameters(),
                              right->GetFunctionReturn()));
        }

        if(left->GetName() != right->GetName()) {
          ProcessError(expression, L"Invalid operation using functions: " +
                       ReplaceSubstring(left->GetName(), L"#", L"->") + L" and " +
                       ReplaceSubstring(right->GetName(), L"#", L"->"));
        }
      }
                      break;

      case VAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Char");
        break;

      case INT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Float");
        break;

      case CLASS_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and " +
                     ReplaceSubstring(right->GetName(), L"#", L"->"));
        break;

      case BOOLEAN_TYPE:
        ProcessError(left_expr, L"Invalid operation using classes: function reference and System.Bool");
        break;
      }
      break;
    }
  }
}

bool ContextAnalyzer::UnboxingCalculation(Type* type, Expression* expression, CalculatedExpression* calc_expression, bool set_left, const int depth)
{
  if(!type || !expression) {
    return false;
  }

  ResolveClassEnumType(type);
  if(expression->GetExpressionType() == VAR_EXPR && IsHolderType(type->GetName())) {
    ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
    MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(expression->GetFileName(), expression->GetLineNumber(),
                                                                          static_cast<Variable*>(expression), L"Get", box_expressions);
    AnalyzeMethodCall(box_method_call, depth + 1);

    if(set_left) {
      calc_expression->SetLeft(box_method_call);
    }
    else {
      calc_expression->SetRight(box_method_call);
    }

    AnalyzeCalculationCast(calc_expression, depth + 1);
    return true;
  }
  else if(expression->GetExpressionType() == METHOD_CALL_EXPR && IsHolderType(type->GetName())) {
    ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
    MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(expression->GetFileName(), expression->GetLineNumber(),
                                                                          expression->GetEvalType()->GetName(), L"Get", box_expressions);
    expression->SetMethodCall(box_method_call);
    AnalyzeExpression(calc_expression, depth + 1);
    return true;
  }

  return false;
}

MethodCall* ContextAnalyzer::BoxUnboxingReturn(Type* to_type, Expression* from_expr, const int depth)
{
  if(to_type && from_expr) {
    ResolveClassEnumType(to_type);

    Type* from_type = from_expr->GetEvalType();
    if(!from_type) {
      from_type = from_expr->GetBaseType();
    }

    if(!from_type) {
      return nullptr;
    }

    ResolveClassEnumType(from_type);

    switch(to_type->GetType()) {
    case BOOLEAN_TYPE:
    case BYTE_TYPE:
    case CHAR_TYPE:
    case INT_TYPE:
    case FLOAT_TYPE: {
      if(from_expr->GetExpressionType() == METHOD_CALL_EXPR && IsHolderType(from_type->GetName())) {
        ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
        MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(),
                                                                              from_expr->GetEvalType()->GetName(), L"Get", box_expressions);
        
        from_expr->SetMethodCall(box_method_call);
        AnalyzeMethodCall(static_cast<MethodCall*>(from_expr), depth);
        return static_cast<MethodCall*>(from_expr);
      }
    }
      break;

    case CLASS_TYPE: {
      switch(from_type->GetType()) {
      case BOOLEAN_TYPE:
      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
      case FLOAT_TYPE:
        if(IsHolderType(to_type->GetName())) {
          ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
          box_expressions->AddExpression(from_expr);
          MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(),
                                                                                NEW_INST_CALL, to_type->GetName(), box_expressions);
          AnalyzeMethodCall(box_method_call, depth);
          return box_method_call;
      }
        break;

      default:
        break;
      }
    }
      break;

    default:
      break;
    }
  }

  return nullptr;
}

/****************************
 * Preforms type conversions for
 * assignment statements.  This
 * method uses execution simulation.
 ****************************/
Expression* ContextAnalyzer::AnalyzeRightCast(Variable* variable, Expression* expression, bool is_scalar, const int depth)
{
  Expression* box_expression = AnalyzeRightCast(variable->GetEvalType(), GetExpressionType(expression, depth + 1), expression, is_scalar, depth);
  if(variable->GetIndices() && !is_scalar) {
    ProcessError(expression, L"Dimension size mismatch");
  }

  return box_expression;
}

Expression* ContextAnalyzer::AnalyzeRightCast(Type* left, Expression* expression, bool is_scalar, const int depth)
{
  return AnalyzeRightCast(left, GetExpressionType(expression, depth + 1), expression, is_scalar, depth);
}

Expression* ContextAnalyzer::AnalyzeRightCast(Type* left, Type* right, Expression* expression, bool is_scalar, const int depth)
{
  // assert(left && right);
  if(!expression || !left || !right) {
    return nullptr;
  }

  // scalar
  if(is_scalar) {
    switch(left->GetType()) {
    case VAR_TYPE:
      // VAR
      switch(right->GetType()) {
      case ALIAS_TYPE:
        break;
          
      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Var and Var");
        break;

      case NIL_TYPE:
      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
      case FLOAT_TYPE:
      case CLASS_TYPE:
      case BOOLEAN_TYPE:
        break;

      default:
        break;
      }
      break;

    case NIL_TYPE:
      // NIL
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Nil and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Nil and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(expression, L"Invalid operation with Nil");
        break;

      case BYTE_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Char");
        break;

      case INT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Float");
        break;

      case CLASS_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and " +
                     ReplaceSubstring(right->GetName(), L"#", L"->"));
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: Nil and System.Bool");
        break;
      }
      break;

    case BYTE_TYPE:
      // BYTE
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Byte and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Byte and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Byte and Nil");
        }
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != FLOAT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case FLOAT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Byte and " +
                         ReplaceSubstring(right->GetName(), L"#", L"->"));
          }
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Byte and System.Bool");
        break;
      }
      break;

    case CHAR_TYPE:
      // CHAR
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Char and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Char and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Char and Nil");
        }
        break;

      case CHAR_TYPE:
      case BYTE_TYPE:
      case INT_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != FLOAT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case FLOAT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Char and " +  
                         ReplaceSubstring(right->GetName(), L"#", L"->"));
          }
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Char and System.Bool");
        break;
      }
      break;

    case INT_TYPE:
      // INT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Int and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Var and Int");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Int and Nil");
        }
        break;

      case INT_TYPE:
      case BYTE_TYPE:
      case CHAR_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != FLOAT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case FLOAT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Int and " +
                         ReplaceSubstring(right->GetName(), L"#", L"->"));
          }
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Int and System.Bool");
        break;
      }
      break;

    case FLOAT_TYPE:
      // FLOAT
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Float and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: Nil and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Float and Nil");
        }
        break;

      case FLOAT_TYPE:
        if(expression->GetEvalType() && expression->GetEvalType()->GetType() != INT_TYPE) {
          expression->SetEvalType(left, false);
        }
        break;

      case BYTE_TYPE:
      case CHAR_TYPE:
      case INT_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case CLASS_TYPE:
        if(!HasProgramLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Float and " +
                         ReplaceSubstring(ReplaceSubstring(right->GetName(), L"#", L"->"), L"#", L"->"));
          }
        }
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Float and System.Bool");
        break;
      }
      break;

    case CLASS_TYPE:
      // CLASS
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: " +
                     ReplaceSubstring(left->GetName(), L"#", L"->") + L" and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: " +
                     ReplaceSubstring(left->GetName(), L"#", L"->") + L" and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        expression->SetCastType(left, false);
        expression->SetEvalType(right, false);
        break;

      case BYTE_TYPE:
        if(!HasProgramLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: " + ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Byte");
          }
        }
        break;

      case CHAR_TYPE:
        if(!HasProgramLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: " + ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Char");
          }
        }
        break;

      case INT_TYPE:
        if(!HasProgramLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: " + ReplaceSubstring(left->GetName(), L"#", L"->") + L" and Int");
          }
        }
        break;

      case FLOAT_TYPE:
        if(!HasProgramLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: " + ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Float");
          }
        }
        break;

      case CLASS_TYPE:
        AnalyzeClassCast(left, expression, depth + 1);
        break;

      case BOOLEAN_TYPE:
        if(!HasProgramLibraryEnum(left->GetName())) {
          Expression* boxed_expression = BoxExpression(left, expression, depth);
          if(boxed_expression) {
            return boxed_expression;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: " + left->GetName() + L" and System.Bool");
          }
        }
        else {
          ProcessError(expression, L"Invalid cast with classes: " + ReplaceSubstring(left->GetName(), L"#", L"->") + L" and System.Bool");
        }
        break;
      }
      break;
      
    case BOOLEAN_TYPE:
      // BOOLEAN
      switch(right->GetType()) {
      case FUNC_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Bool and function reference");
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: System.Bool and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        if(left->GetDimension() < 1) {
          ProcessError(expression, L"Invalid cast with classes: System.Bool and Nil");
        }
        break;

      case BYTE_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and System.Char");
        break;

      case INT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: System.Bool and System.Float");
        break;

      case CLASS_TYPE:
        if(!HasProgramLibraryEnum(right->GetName())) {
          Expression* unboxed_expresion = UnboxingExpression(right, expression, true, depth);
          if(unboxed_expresion) {
            return unboxed_expresion;
          }
          else {
            ProcessError(expression, L"Invalid cast with classes: System.Bool and " +
                         ReplaceSubstring(ReplaceSubstring(right->GetName(), L"#", L"->"), L"#", L"->"));
          }
        }
        break;
        
      case BOOLEAN_TYPE:
        break;
      }
      break;

    case FUNC_TYPE:
      // FUNCTION
      switch(right->GetType()) {
      case FUNC_TYPE: {
        AnalyzeVariableFunctionParameters(left, expression);
        if(left->GetName().size() == 0) {
          left->SetName(L"m." + EncodeFunctionType(left->GetFunctionParameters(), left->GetFunctionReturn()));
        }

        if(right->GetName().size() == 0) {
          right->SetName(L"m." + EncodeFunctionType(right->GetFunctionParameters(), right->GetFunctionReturn()));
        }
      }
        break;

      case VAR_TYPE:
        ProcessError(expression, L"Invalid operation using classes: function reference and Var");
        break;
          
      case ALIAS_TYPE:
        break;

      case NIL_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and Nil");
        break;

      case BYTE_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Byte");
        break;

      case CHAR_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Char");
        break;

      case INT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and Int");
        break;

      case FLOAT_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Float");
        break;

      case CLASS_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and " +
                     ReplaceSubstring(ReplaceSubstring(right->GetName(), L"#", L"->"), L"#", L"->"));
        break;

      case BOOLEAN_TYPE:
        ProcessError(expression, L"Invalid cast with classes: function reference and System.Bool");
        break;
      }
      break;

    default:
      break;
    }
  }
  // multi-dimensional
  else {
    if(left->GetDimension() != right->GetDimension() &&
       right->GetType() != NIL_TYPE) {
      ProcessError(expression, L"Dimension size mismatch");
    }

    if(left->GetType() != right->GetType() &&
       right->GetType() != NIL_TYPE) {
      ProcessError(expression, L"Invalid array cast");
    }

    if(left->GetType() == CLASS_TYPE && right->GetType() == CLASS_TYPE) {
      AnalyzeClassCast(left, expression, depth + 1);
    }

    expression->SetEvalType(left, false);
  }

  return nullptr;
}

//
// Unboxing expression
//
Expression* ContextAnalyzer::UnboxingExpression(Type* to_type, Expression* from_expr, bool is_cast, int depth)
{
  if(!to_type || !from_expr) {
    return nullptr;
  }
  
  Type* from_type = GetExpressionType(from_expr, depth);
  if(!from_type) {
    return nullptr;
  }
  
  ResolveClassEnumType(to_type);
  ResolveClassEnumType(from_type);
  
  if(to_type->GetType() == CLASS_TYPE && (from_type->GetType() != CLASS_TYPE || is_cast)) {
    if(from_expr->GetExpressionType() == VAR_EXPR && IsHolderType(to_type->GetName())) {
      MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(),
                                                                            static_cast<Variable*>(from_expr), L"Get",
                                                                            TreeFactory::Instance()->MakeExpressionList());
      AnalyzeMethodCall(box_method_call, depth);
      return box_method_call;
    }
    else if(from_expr->GetExpressionType() == METHOD_CALL_EXPR && IsHolderType(to_type->GetName())) {
      MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(),
                                                                            from_expr->GetEvalType()->GetName(), L"Get",
                                                                            TreeFactory::Instance()->MakeExpressionList());
      AnalyzeMethodCall(box_method_call, depth);
      from_expr->SetMethodCall(box_method_call);
      return from_expr;
    }
  }

  return nullptr;
}

//
// Boxing expression
//
Expression* ContextAnalyzer::BoxExpression(Type* to_type, Expression* from_expr, int depth)
{
  if(!to_type || !from_expr) {
    return nullptr;
  }

  ResolveClassEnumType(to_type);

  Type* from_type = GetExpressionType(from_expr, depth);
  if(!from_type) {
    return nullptr;
  }

  const bool is_enum = from_expr->GetExpressionType() == METHOD_CALL_EXPR && static_cast<MethodCall*>(from_expr)->GetEnumItem();
  if(to_type->GetType() == CLASS_TYPE && (is_enum ||
     from_type->GetType() == BOOLEAN_TYPE || 
     from_type->GetType() == BYTE_TYPE || 
     from_type->GetType() == CHAR_TYPE || 
     from_type->GetType() == INT_TYPE || 
     from_type->GetType() == FLOAT_TYPE)) {
    if(IsHolderType(to_type->GetName())) {
      ExpressionList* box_expressions = TreeFactory::Instance()->MakeExpressionList();
      box_expressions->AddExpression(from_expr);
      MethodCall* box_method_call = TreeFactory::Instance()->MakeMethodCall(from_expr->GetFileName(), from_expr->GetLineNumber(),
                                                                            NEW_INST_CALL, to_type->GetName(), box_expressions);
      AnalyzeMethodCall(box_method_call, depth);
      return box_method_call;
    }
  }

  return nullptr;
}

/****************************
 * Analyzes a class cast. Up
 * casting is resolved a runtime.
 ****************************/
void ContextAnalyzer::AnalyzeClassCast(Type* left, Expression* expression, const int depth)
{
  if(expression->GetCastType() && expression->GetEvalType() && (expression->GetCastType()->GetType() != CLASS_TYPE ||
     expression->GetEvalType()->GetType() != CLASS_TYPE)) {
    AnalyzeRightCast(expression->GetCastType(), expression->GetEvalType(), expression, IsScalar(expression), depth + 1);
  }

  Type* right = expression->GetCastType();
  if(!right) {
    right = expression->GetEvalType();
  }

  AnalyzeClassCast(left, right, expression, false, depth);
}

void ContextAnalyzer::AnalyzeClassCast(Type* left, Type* right, Expression* expression, bool generic_check, const int depth)
{
  Class* left_class = nullptr;
  LibraryEnum* left_lib_enum = nullptr;
  LibraryClass* left_lib_class = nullptr;
  
  if(current_class->HasGenerics() || left->HasGenerics() || right->HasGenerics()) {
    CheckGenericEqualTypes(left, right, expression);
  }
  
  if(current_class->HasGenerics()) {
    Class* left_tmp = current_class->GetGenericClass(left->GetName());
    if(left_tmp && left_tmp->HasGenericInterface()) {
      left = left_tmp->GetGenericInterface();
    }

    Class* right_tmp = current_class->GetGenericClass(right->GetName());
    if(right_tmp && right_tmp->HasGenericInterface()) {
      right = right_tmp->GetGenericInterface();
    }
  }

  //
  // program enum
  //
  Enum* left_enum = SearchProgramEnums(left->GetName());
  if(!left_enum) {
    left_enum = SearchProgramEnums(current_class->GetName() + L"#" + left->GetName());
  }

  if(right && left_enum) {
    // program
    Enum* right_enum = SearchProgramEnums(right->GetName());
    if(right_enum) {
      if(left_enum->GetName() != right_enum->GetName()) {
        const wstring left_str = ReplaceSubstring(left->GetName(), L"#", L"->");
        const wstring right_str = ReplaceSubstring(right->GetName(), L"#", L"->");
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    // library
    else if(right && linker->SearchEnumLibraries(right->GetName(), program->GetUses(current_class->GetFileName()))) {
      LibraryEnum* right_lib_enum = linker->SearchEnumLibraries(right->GetName(), program->GetUses(current_class->GetFileName()));
      if(left_enum->GetName() != right_lib_enum->GetName()) {
        const wstring left_str = ReplaceSubstring(left->GetName(), L"#", L"->");
        const wstring right_str = ReplaceSubstring(right->GetName(), L"#", L"->");
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between enum and class");
    }
  }
  //
  // program class
  //
  else if(right && (left_class = SearchProgramClasses(left->GetName()))) {
    // program and generic
    Class* right_class = SearchProgramClasses(right->GetName());
    if(!right_class) {
      right_class = current_class->GetGenericClass(right->GetName());
    }
    if(right_class) {
      // downcast
      if(ValidDownCast(left_class->GetName(), right_class, nullptr)) {
        left_class->SetCalled(true);
        right_class->SetCalled(true);
        if(left_class->IsInterface() && !generic_check) {
          expression->SetToClass(left_class);
        }
        return;
      }
      // upcast
      else if(right_class->IsInterface() || ValidUpCast(left_class->GetName(), right_class)) {
        expression->SetToClass(left_class);
        left_class->SetCalled(true);
        right_class->SetCalled(true);
        return;
      }
      // invalid cast
      else {
        expression->SetToClass(left_class);
        ProcessError(expression, L"Invalid cast between classes: '" +
                     ReplaceSubstring(left->GetName(), L"#", L"->") + L"' and '" +
                     ReplaceSubstring(right->GetName(), L"#", L"->") + L"'");
      }
    }
    // library
    else if(linker->SearchClassLibraries(right->GetName(), program->GetUses(current_class->GetFileName()))) {
      LibraryClass* right_lib_class = linker->SearchClassLibraries(right->GetName(), program->GetUses(current_class->GetFileName()));
      // downcast
      if(ValidDownCast(left_class->GetName(), nullptr, right_lib_class)) {
        if(left_class->IsInterface() && !generic_check) {
          expression->SetToClass(left_class);
        }
        return;
      }
      // upcast
      else if(right_lib_class && (right_lib_class->IsInterface() || ValidUpCast(left_class->GetName(), right_lib_class))) {
        expression->SetToClass(left_class);
        return;
      }
      // invalid cast
      else {
        expression->SetToClass(left_class);
        ProcessError(expression, L"Invalid cast between classes: '" +
                     ReplaceSubstring(left->GetName(), L"#", L"->") + L"' and '" +
                     ReplaceSubstring(right->GetName(), L"#", L"->") + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between class, enum or return type");
    }
  }
  //
  // generic class
  //
  else if(right && (left_class = current_class->GetGenericClass(left->GetName()))) {
    // program
    Class* right_class = current_class->GetGenericClass(right->GetName());
    if(right_class) {
      if(left->GetName() == right->GetName()) {
        return;
      }
      else {
        ProcessError(expression, L"Invalid cast between generics: '" +
                     ReplaceSubstring(left->GetName(), L"#", L"->") + L"' and '" +
                     ReplaceSubstring(right->GetName(), L"#", L"->") + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between generic: '" +
                   ReplaceSubstring(left->GetName(), L"#", L"->") + L"' and class/enum '" +
                   ReplaceSubstring(right->GetName(), L"#", L"->") + L"'");
    }
  }
  //
  // enum library
  //
  else if(right && (left_lib_enum = linker->SearchEnumLibraries(left->GetName(), program->GetUses(current_class->GetFileName())))) {
    // program
    Enum* right_enum = SearchProgramEnums(right->GetName());
    if(right_enum) {
      if(left_lib_enum->GetName() != right_enum->GetName()) {
        const wstring left_str = ReplaceSubstring(left_lib_enum->GetName(), L"#", L"->");
        const wstring right_str = ReplaceSubstring(right_enum->GetName(), L"#", L"->");
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    // library
    else if(linker->SearchEnumLibraries(right->GetName(), program->GetUses(current_class->GetFileName()))) {
      LibraryEnum* right_lib_enum = linker->SearchEnumLibraries(right->GetName(), program->GetUses(current_class->GetFileName()));
      if(left_lib_enum->GetName() != right_lib_enum->GetName()) {
        const wstring left_str = ReplaceSubstring(left_lib_enum->GetName(), L"#", L"->");
        const wstring right_str = ReplaceSubstring(right_lib_enum->GetName(), L"#", L"->");
        ProcessError(expression, L"Invalid cast between enums: '" + left_str + L"' and '" + right_str + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between enum and class");
    }
  }
  //
  // class library
  //
  else if(right && (left_lib_class = linker->SearchClassLibraries(left->GetName(), program->GetUses(current_class->GetFileName())))) {
    // program and generic
    Class* right_class = SearchProgramClasses(right->GetName());
    if(!right_class) {
      right_class = current_class->GetGenericClass(right->GetName());
    }
    if(right_class) {
      // downcast
      if(ValidDownCast(left_lib_class->GetName(), right_class, nullptr)) {
        left_lib_class->SetCalled(true);
        right_class->SetCalled(true);
        if(left_lib_class->IsInterface() && !generic_check) {
          expression->SetToLibraryClass(left_lib_class);
        }
        return;
      }
      // upcast
      else if(right_class->IsInterface() || ValidUpCast(left_lib_class->GetName(), right_class)) {
        expression->SetToLibraryClass(left_lib_class);
        left_lib_class->SetCalled(true);
        right_class->SetCalled(true);
        return;
      }
      // invalid cast
      else {
        ProcessError(expression, L"Invalid cast between classes: '" + ReplaceSubstring(left->GetName(), L"#", L"->") +
                     L"' and '" + ReplaceSubstring(right->GetName(), L"#", L"->") + L"'");
      }
    }
    // library
    else if(linker->SearchClassLibraries(right->GetName(), program->GetUses(current_class->GetFileName()))) {
      LibraryClass* right_lib_class = linker->SearchClassLibraries(right->GetName(), program->GetUses(current_class->GetFileName()));
      // downcast
      if(ValidDownCast(left_lib_class->GetName(), nullptr, right_lib_class)) {
        left_lib_class->SetCalled(true);
        right_lib_class->SetCalled(true);
        if(left_lib_class->IsInterface() && !generic_check) {
          expression->SetToLibraryClass(left_lib_class);
        }
        return;
      }
      // upcast
      else if(right_lib_class && (right_lib_class->IsInterface() || ValidUpCast(left_lib_class->GetName(), right_lib_class))) {
        expression->SetToLibraryClass(left_lib_class);
        left_lib_class->SetCalled(true);
        right_lib_class->SetCalled(true);
        return;
      }
      // downcast
      else {
        ProcessError(expression, L"Invalid cast between classes: '" + left_lib_class->GetName() + L"' and '" +
                     right_lib_class->GetName() + L"'");
      }
    }
    else {
      ProcessError(expression, L"Invalid cast between class or enum: '" + 
                   left->GetName() + L"' and '" + right->GetName() + L"'");
    }
  }
  else {
    ProcessError(expression, L"Invalid class, enum or method call context\n\tEnsure all required libraries have been included");
  }
}

bool ContextAnalyzer::CheckGenericEqualTypes(Type* left, Type* right, Expression* expression, bool check_only)
{
  // note, enums and consts checked elsewhere
  Class* left_klass = nullptr; LibraryClass* lib_left_klass = nullptr;
  if(!GetProgramLibraryClass(left, left_klass, lib_left_klass) && !current_class->GetGenericClass(left->GetName())) {
    return false;
  }

  // note, enums and consts checked elsewhere
  Class* right_klass = nullptr; LibraryClass* lib_right_klass = nullptr;
  if(!GetProgramLibraryClass(right, right_klass, lib_right_klass) && !current_class->GetGenericClass(right->GetName())) {
    return false;
  }

  if(left_klass == right_klass && lib_left_klass == lib_right_klass) {
    const vector<Type*> left_generic_types = left->GetGenerics();
    const vector<Type*> right_generic_types = right->GetGenerics();
    if(left_generic_types.size() != right_generic_types.size()) {
      if(check_only) {
        return false;
      }
      ProcessError(expression, L"Concrete size mismatch");
    }
    else {
      for(size_t i = 0; i < right_generic_types.size(); ++i) {
        // process lhs
        Type* left_generic_type = left_generic_types[i];
        ResolveClassEnumType(left_generic_type);

        Class* left_generic_klass = nullptr; LibraryClass* lib_generic_left_klass = nullptr;
        if(GetProgramLibraryClass(left_generic_type, left_generic_klass, lib_generic_left_klass)) {
          if(left_generic_klass && left_generic_klass->HasGenericInterface()) {
            left_generic_type = left_generic_klass->GetGenericInterface();
          }
          else if(lib_generic_left_klass && lib_generic_left_klass->HasGenericInterface()) {
            left_generic_type = lib_generic_left_klass->GetGenericInterface();
          }
        }
        else {
          left_generic_klass = current_class->GetGenericClass(left_generic_type->GetName());
          if(left_generic_klass && left_generic_klass->HasGenericInterface()) {
            left_generic_type = left_generic_klass->GetGenericInterface();
          }
          else {
            left_generic_type = ResolveGenericType(left_generic_type, expression, left_klass, lib_left_klass);
          }
        }
        
        // process rhs
        Type* right_generic_type = right_generic_types[i];
        ResolveClassEnumType(right_generic_type);

        Class* right_generic_klass = nullptr; LibraryClass* lib_generic_right_klass = nullptr;
        if(GetProgramLibraryClass(right_generic_type, right_generic_klass, lib_generic_right_klass)) {
          if(right_generic_klass && right_generic_klass->HasGenericInterface()) {
            right_generic_type = right_generic_klass->GetGenericInterface();
          }
          else if(lib_generic_right_klass && lib_generic_right_klass->HasGenericInterface()) {
            right_generic_type = lib_generic_right_klass->GetGenericInterface();
          }
        }
        else {
          right_generic_klass = current_class->GetGenericClass(right_generic_type->GetName());
          if(right_generic_klass && right_generic_klass->HasGenericInterface()) {
            right_generic_type = right_generic_klass->GetGenericInterface();
          }
          else {
            right_generic_type = ResolveGenericType(right_generic_type, expression, right_klass, lib_right_klass);
          }
        }

        const wstring left_type_name = left_generic_type->GetName();
        const wstring right_type_name = right_generic_type->GetName();
        if(left_type_name != right_type_name) {
          if(check_only) {
            return false;
          }
          ProcessError(expression, L"Cannot map generic/concrete class to concrete class: '" + left_type_name + L"' and '" + right_type_name + L"'");
        }
      }
    }
  }

  return true;
}

/****************************
 * Analyzes a declaration
 ****************************/
void ContextAnalyzer::AnalyzeDeclaration(Declaration * declaration, Class* klass, const int depth)
{
  SymbolEntry* entry = declaration->GetEntry();
  if(entry) {
    if(entry->GetType() && entry->GetType()->GetType() == CLASS_TYPE) {
      // resolve declaration type
      Type* type = entry->GetType();
      if(!ResolveClassEnumType(type, klass)) {
        ProcessError(entry, L"Undefined class or enum: '" + ReplaceSubstring(type->GetName(), L"#", L"->") + L"'\n\tIf generic ensure concrete types are properly defined.");
      }

      ValidateConcrete(type, type, declaration, depth);
    }
    else if(entry->GetType() && entry->GetType()->GetType() == FUNC_TYPE) {
      // resolve function name
      Type* type = entry->GetType();
      AnalyzeVariableFunctionParameters(type, entry, klass);
      const wstring encoded_name = L"m." + EncodeFunctionType(type->GetFunctionParameters(), type->GetFunctionReturn());
#ifdef _DEBUG
      GetLogger() << L"Encoded function declaration: |" << encoded_name << L"|" << endl;
#endif
      type->SetName(encoded_name);
    }

    Statement* statement = declaration->GetAssignment();
    if(entry->IsStatic()) {
      if(current_method) {
        ProcessError(entry, L"Static variables can only be declared at class scope");
      }

      if(statement) {
        ProcessError(entry, L"Variables cannot be initialized at class scope");
      }
    }

    if(!entry->IsLocal() && statement) {
      ProcessError(entry, L"Variables cannot be initialized at class scope");
    }

    if(statement) {
      AnalyzeStatement(statement, depth);
    }
  }
  else {
    ProcessError(declaration, L"Undefined variable entry");
  }
}

/****************************
 * Analyzes a declaration
 ****************************/
void ContextAnalyzer::AnalyzeExpressions(ExpressionList* parameters, const int depth)
{
  vector<Expression*> expressions = parameters->GetExpressions();
  for(size_t i = 0; i < expressions.size(); ++i) {
    AnalyzeExpression(expressions[i], depth);
  }
}

/********************************
 * Encodes a function definition
 ********************************/
wstring ContextAnalyzer::EncodeFunctionReference(ExpressionList* calling_params, const int depth)
{
  wstring encoded_name;
  vector<Expression*> expressions = calling_params->GetExpressions();
  for(size_t i = 0; i < expressions.size(); ++i) {
    if(expressions[i]->GetExpressionType() == VAR_EXPR) {
      Variable* variable = static_cast<Variable*>(expressions[i]);
      if(variable->GetName() == BOOL_CLASS_ID) {
        encoded_name += L'l';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(BOOLEAN_TYPE), true);
      }
      else if(variable->GetName() == BYTE_CLASS_ID) {
        encoded_name += L'b';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(BYTE_TYPE), true);
      }
      else if(variable->GetName() == INT_CLASS_ID) {
        encoded_name += L'i';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(INT_TYPE), true);
      }
      else if(variable->GetName() == FLOAT_CLASS_ID) {
        encoded_name += L'f';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(FLOAT_TYPE), true);
      }
      else if(variable->GetName() == CHAR_CLASS_ID) {
        encoded_name += L'c';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(CHAR_TYPE), true);
      }
      else if(variable->GetName() == NIL_CLASS_ID) {
        encoded_name += L'n';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(NIL_TYPE), true);
      }
      else if(variable->GetName() == VAR_CLASS_ID) {
        encoded_name += L'v';
        variable->SetEvalType(TypeFactory::Instance()->MakeType(VAR_TYPE), true);
      }
      else {
        encoded_name += L"o.";
        // search program
        const wstring klass_name = variable->GetName();
        Class* klass = program->GetClass(klass_name);
        if(!klass) {
          vector<wstring> uses = program->GetUses(current_class->GetFileName());
          for(size_t i = 0; !klass && i < uses.size(); ++i) {
            klass = program->GetClass(uses[i] + L"." + klass_name);
          }
        }
        if(klass) {
          encoded_name += klass->GetName();
          variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, klass->GetName()), true);
        }
        // search libaraires
        else {
          LibraryClass* lib_klass = linker->SearchClassLibraries(klass_name, program->GetUses(current_class->GetFileName()));
          if(lib_klass) {
            encoded_name += lib_klass->GetName();
            variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_klass->GetName()), true);
          }
          else {
            encoded_name += variable->GetName();
            variable->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, variable->GetName()), true);
          }
        }
      }

      // dimension
      if(variable->GetIndices()) {
        vector<Expression*> indices = variable->GetIndices()->GetExpressions();
        variable->GetEvalType()->SetDimension((int)indices.size());
        for(size_t j = 0; j < indices.size(); ++j) {
          encoded_name += L'*';
        }
      }

      encoded_name += L',';
    }
    else {
      // induce error condition
      encoded_name += L'#';
    }
  }

  return encoded_name;
}

/****************************
 * Encodes a function type
 ****************************/
wstring ContextAnalyzer::EncodeFunctionType(vector<Type*> func_params, Type* func_rtrn) {
  wstring encoded_name = L"(";
  for(size_t i = 0; i < func_params.size(); ++i) {
    // encode params
    encoded_name += EncodeType(func_params[i]);

    // encode dimension
    for(int j = 0; j < func_params[i]->GetDimension(); ++j) {
      encoded_name += L'*';
    }
    encoded_name += L',';
  }

  // encode return
  encoded_name += L")~";
  encoded_name += EncodeType(func_rtrn);
  // encode dimension
  for(int i = 0; func_rtrn && i < func_rtrn->GetDimension(); ++i) {
    encoded_name += L'*';
  }

  return encoded_name;
}

/****************************
 * Encodes a method call
 ****************************/
wstring ContextAnalyzer::EncodeMethodCall(ExpressionList* calling_params, const int depth)
{
  wstring encoded_name;
  vector<Expression*> expressions = calling_params->GetExpressions();
  for(size_t i = 0; i < expressions.size(); ++i) {
    Expression* expression = expressions[i];
    while(expression->GetMethodCall()) {
      AnalyzeExpressionMethodCall(expression, depth + 1);
      expression = expression->GetMethodCall();
    }

    Type* type;
    if(expression->GetCastType()) {
      type = expression->GetCastType();
    }
    else {
      type = expression->GetEvalType();
    }

    if(type) {
      // encode params
      encoded_name += EncodeType(type);

      // encode dimension
      for(int j = 0; !IsScalar(expression) && j < type->GetDimension(); ++j) {
        encoded_name += L'*';
      }
      encoded_name += L',';
    }
  }

  return encoded_name;
}

bool ContextAnalyzer::IsScalar(Expression* expression, bool check_last /*= true*/)
{
  while(check_last && expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }

  Type* type;
  if(expression->GetCastType() && !(expression->GetEvalType() && expression->GetEvalType()->GetDimension() > 0) ) {
    type = expression->GetCastType();
  }
  else {
    type = expression->GetEvalType();
  }

  if(type && type->GetDimension() > 0) {
    ExpressionList* indices = nullptr;
    if(expression->GetExpressionType() == VAR_EXPR) {
      indices = static_cast<Variable*>(expression)->GetIndices();
    }
    else {
      return false;
    }

    return indices != nullptr;
  }

  return true;
}

bool ContextAnalyzer::IsBooleanExpression(Expression* expression)
{
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }
  Type* eval_type = expression->GetEvalType();
  if(eval_type) {
    return eval_type->GetType() == BOOLEAN_TYPE;
  }

  return false;
}

bool ContextAnalyzer::IsEnumExpression(Expression* expression)
{
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }
  Type* eval_type = expression->GetEvalType();
  if(eval_type) {
    if(eval_type->GetType() == CLASS_TYPE) {
      // program
      if(program->GetEnum(eval_type->GetName())) {
        return true;
      }
      // library
      if(linker->SearchEnumLibraries(eval_type->GetName(), program->GetUses())) {
        return true;
      }
    }
  }

  return false;
}

bool ContextAnalyzer::IsIntegerExpression(Expression* expression)
{
  while(expression->GetMethodCall()) {
    expression = expression->GetMethodCall();
  }

  Type* eval_type;
  if(expression->GetCastType()) {
    eval_type = expression->GetCastType();
  }
  else {
    eval_type = expression->GetEvalType();
  }

  if(eval_type) {
    // integer types
    if(eval_type->GetType() == INT_TYPE ||
       eval_type->GetType() == CHAR_TYPE ||
       eval_type->GetType() == BYTE_TYPE) {
      return true;
    }
    // enum types
    if(eval_type->GetType() == CLASS_TYPE) {
      // program
      if(SearchProgramEnums(eval_type->GetName())) {
        return true;
      }
      // library
      if(linker->SearchEnumLibraries(eval_type->GetName(), program->GetUses())) {
        return true;
      }
    }
  }

  return false;
}

bool ContextAnalyzer::DuplicateParentEntries(SymbolEntry* entry, Class* klass)
{
  if(klass->GetParent() && klass->GetParent()->GetSymbolTable() && (!entry->IsLocal() || entry->IsStatic())) {
    Class* parent = klass->GetParent();
    do {
      size_t offset = entry->GetName().find(L':');
      if(offset != wstring::npos) {
        ++offset;
        const wstring short_name = entry->GetName().substr(offset, entry->GetName().size() - offset);
        const wstring lookup = parent->GetName() + L':' + short_name;
        SymbolEntry * parent_entry = parent->GetSymbolTable()->GetEntry(lookup);
        if(parent_entry) {
          return true;
        }
      }
      // update
      parent = parent->GetParent();
    } while(parent);
  }

  return false;
}

bool ContextAnalyzer::DuplicateCaseItem(map<int, StatementList*>label_statements, int value)
{
  map<int, StatementList*>::iterator result = label_statements.find(value);
  if(result != label_statements.end()) {
    return true;
  }

  return false;
}

bool ContextAnalyzer::InvalidStatic(MethodCall* method_call, Method* method)
{
  // same class, calling method static and called method not static,
  // called method not new, called method not from a variable
  if(current_method->IsStatic() &&
     !method->IsStatic() && method->GetMethodType() != NEW_PUBLIC_METHOD &&
     method->GetMethodType() != NEW_PRIVATE_METHOD) {
    SymbolEntry* entry = GetEntry(method_call->GetVariableName());
    if(entry && (entry->IsLocal() || entry->IsStatic())) {
      return false;
    }

    Variable* variable = method_call->GetVariable();
    if(variable) {
      entry = variable->GetEntry();
      if(entry && (entry->IsLocal() || entry->IsStatic())) {
        return false;
      }
    }

    return true;
  }

  return false;
}

SymbolEntry* ContextAnalyzer::GetEntry(wstring name, bool is_parent)
{
  if(current_table) {
    // check locally
    SymbolEntry* entry = current_table->GetEntry(current_method->GetName() + L':' + name);
    if(!is_parent && entry) {
      return entry;
    }
    else {
      // check class
      SymbolTable* table = symbol_table->GetSymbolTable(current_class->GetName());
      entry = table->GetEntry(current_class->GetName() + L':' + name);
      if(!is_parent && entry) {
        return entry;
      }
      else {
        // check parents
        entry = nullptr;
        const wstring& bundle_name = bundle->GetName();
        Class* parent;
        if(bundle_name.size() > 0) {
          parent = bundle->GetClass(bundle_name + L"." + current_class->GetParentName());
        }
        else {
          parent = bundle->GetClass(current_class->GetParentName());
        }
        while(parent && !entry) {
          SymbolTable* table = symbol_table->GetSymbolTable(parent->GetName());
          entry = table->GetEntry(parent->GetName() + L':' + name);
          if(entry) {
            return entry;
          }
          // get next parent    
          if(bundle_name.size() > 0) {
            parent = bundle->GetClass(bundle_name + L"." + parent->GetParentName());
          }
          else {
            parent = bundle->GetClass(parent->GetParentName());
          }
        }
      }
    }
  }

  return nullptr;
}

SymbolEntry* ContextAnalyzer::GetEntry(MethodCall* method_call, const wstring& variable_name, int depth)
{
  SymbolEntry* entry;
  if(method_call->GetVariable()) {
    Variable* variable = method_call->GetVariable();
    AnalyzeVariable(variable, depth);
    entry = variable->GetEntry();
  }
  else {
    entry = GetEntry(variable_name);
    if(entry) {
      method_call->SetEntry(entry);
    }
  }

  return entry;
}

Type* ContextAnalyzer::GetExpressionType(Expression* expression, int depth)
{
  Type* type = nullptr;

  MethodCall* mthd_call = expression->GetMethodCall();

  if(expression->GetExpressionType() == METHOD_CALL_EXPR &&
     static_cast<MethodCall*>(expression)->GetCallType() == ENUM_CALL) {
    // favor casts
    if(expression->GetCastType()) {
      type = expression->GetCastType();
    }
    else {
      type = expression->GetEvalType();
    }
  }
  else if(mthd_call) {
    while(mthd_call) {
      AnalyzeExpressionMethodCall(mthd_call, depth + 1);

      // favor casts
      if(mthd_call->GetCastType()) {
        type = mthd_call->GetCastType();
      }
      else {
        type = mthd_call->GetEvalType();
      }

      mthd_call = mthd_call->GetMethodCall();
    }
  }
  else {
    // favor casts
    if(expression->GetCastType()) {
      type = expression->GetCastType();
    }
    else {
      type = expression->GetEvalType();
    }
  }

  return type;
}

bool ContextAnalyzer::ValidDownCast(const wstring& cls_name, Class* class_tmp, LibraryClass* lib_class_tmp)
{
  if(cls_name == L"System.Base") {
    return true;
  }

  while(class_tmp || lib_class_tmp) {
    // get cast name
    wstring cast_name;
    vector<wstring> interface_names;
    if(class_tmp) {
      cast_name = class_tmp->GetName();
      interface_names = class_tmp->GetInterfaceNames();
    }
    else if(lib_class_tmp) {
      cast_name = lib_class_tmp->GetName();
      interface_names = lib_class_tmp->GetInterfaceNames();
    }

    // parent cast
    if(cls_name == cast_name) {
      return true;
    }

    // interface cast
    for(size_t i = 0; i < interface_names.size(); ++i) {
      Class* klass = SearchProgramClasses(interface_names[i]);
      if(klass && klass->GetName() == cls_name) {
        return true;
      }
      else {
        LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetUses());
        if(lib_klass && lib_klass->GetName() == cls_name) {
          return true;
        }
      }
    }

    // update
    if(class_tmp) {
      if(class_tmp->GetParent()) {
        class_tmp = class_tmp->GetParent();
        lib_class_tmp = nullptr;
      }
      else {
        lib_class_tmp = class_tmp->GetLibraryParent();
        class_tmp = nullptr;
      }

    }
    // library parent
    else {
      lib_class_tmp = linker->SearchClassLibraries(lib_class_tmp->GetParentName(), program->GetUses());
      class_tmp = nullptr;
    }
  }

  return false;
}

bool ContextAnalyzer::ValidUpCast(const wstring& to, Class* from_klass)
{
  if(from_klass->GetName() == L"System.Base") {
    return true;
  }

  // parent cast
  if(to == from_klass->GetName()) {
    return true;
  }

  // interface cast
  vector<wstring> interface_names = from_klass->GetInterfaceNames();
  for(size_t i = 0; i < interface_names.size(); ++i) {
    Class* klass = SearchProgramClasses(interface_names[i]);
    if(klass && klass->GetName() == to) {
      return true;
    }
    else {
      LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetUses());
      if(lib_klass && lib_klass->GetName() == to) {
        return true;
      }
    }
  }

  // updates
  vector<Class*> children = from_klass->GetChildren();
  for(size_t i = 0; i < children.size(); ++i) {
    if(ValidUpCast(to, children[i])) {
      return true;
    }
  }

  return false;
}

bool ContextAnalyzer::ValidUpCast(const wstring& to, LibraryClass* from_klass)
{
  if(from_klass->GetName() == L"System.Base") {
    return true;
  }

  // parent cast
  if(to == from_klass->GetName()) {
    return true;
  }

  // interface cast
  vector<wstring> interface_names = from_klass->GetInterfaceNames();
  for(size_t i = 0; i < interface_names.size(); ++i) {
    Class* klass = SearchProgramClasses(interface_names[i]);
    if(klass && klass->GetName() == to) {
      return true;
    }
    else {
      LibraryClass* lib_klass = linker->SearchClassLibraries(interface_names[i], program->GetUses());
      if(lib_klass && lib_klass->GetName() == to) {
        return true;
      }
    }
  }

  // program updates
  vector<LibraryClass*> children = from_klass->GetLibraryChildren();
  for(size_t i = 0; i < children.size(); ++i) {
    if(ValidUpCast(to, children[i])) {
      return true;
    }
  }

  // library updates
  vector<ParseNode*> lib_children = from_klass->GetChildren();
  for(size_t i = 0; i < lib_children.size(); ++i) {
    if(ValidUpCast(to, static_cast<Class*>(lib_children[i]))) {
      return true;
    }
  }

  return false;
}

bool ContextAnalyzer::GetProgramLibraryClass(const wstring &cls_name, Class*& klass, LibraryClass*& lib_klass)
{
  klass = SearchProgramClasses(cls_name);
  if(klass) {
    return true;
  }

  lib_klass = linker->SearchClassLibraries(cls_name, program->GetUses(current_class->GetFileName()));
  if(lib_klass) {
    return true;
  }

  return false;
}

bool ContextAnalyzer::GetProgramLibraryClass(Type* type, Class*& klass, LibraryClass*& lib_klass)
{
  Class* cls_ptr = static_cast<Class*>(type->GetClassPtr());
  if(cls_ptr) {
    klass = cls_ptr;
    return true;
  }

  LibraryClass* lib_cls_ptr = static_cast<LibraryClass*>(type->GetLibraryClassPtr());
  if(lib_cls_ptr) {
    lib_klass = lib_cls_ptr;
    return true;
  }

  if(GetProgramLibraryClass(type->GetName(), klass, lib_klass)) {
    if(klass) {
      type->SetName(klass->GetName());
      type->SetClassPtr(klass);
      type->SetResolved(true);
    }
    else {
      type->SetName(lib_klass->GetName());
      type->SetLibraryClassPtr(lib_klass);
      type->SetResolved(true);
    }

    return true;
  }

  return false;
}

wstring ContextAnalyzer::GetProgramLibraryClassName(const wstring& n)
{
  Class* klass = nullptr; LibraryClass* lib_klass = nullptr;
  if(GetProgramLibraryClass(n, klass, lib_klass)) {
    if(klass) {
      return klass->GetName();
    }
    else {
      return lib_klass->GetName();
    }
  }

  return n;
}

const wstring ContextAnalyzer::EncodeType(Type* type)
{
  wstring encoded_name;

  if(type) {
    switch(type->GetType()) {
    case BOOLEAN_TYPE:
      encoded_name += 'l';
      break;

    case BYTE_TYPE:
      encoded_name += 'b';
      break;

    case INT_TYPE:
      encoded_name += 'i';
      break;

    case FLOAT_TYPE:
      encoded_name += 'f';
      break;

    case CHAR_TYPE:
      encoded_name += 'c';
      break;

    case NIL_TYPE:
      encoded_name += 'n';
      break;

    case VAR_TYPE:
      encoded_name += 'v';
      break;
        
    case ALIAS_TYPE:
      break;

    case CLASS_TYPE: {
      encoded_name += L"o.";

      // search program and libraries
      Class* klass = nullptr; LibraryClass* lib_klass = nullptr;
      if(GetProgramLibraryClass(type, klass, lib_klass)) {
        if(klass) {
          encoded_name += klass->GetName();
        }
        else {
          encoded_name += lib_klass->GetName();
        }
      }
      else {
        encoded_name += type->GetName();
      }
    }
      break;
      
    case FUNC_TYPE:
      if(type->GetName().size() == 0) {
        type->SetName(EncodeFunctionType(type->GetFunctionParameters(), type->GetFunctionReturn()));
      }
      encoded_name += type->GetName();
      break;
    }
  }
  
  return encoded_name;
}

bool ContextAnalyzer::ResolveClassEnumType(Type* type, Class* context_klass)
{
  if(type->IsResolved()) {
    return true;
  }
  
  Class* klass = SearchProgramClasses(type->GetName());
  if(klass) {
    klass->SetCalled(true);
    type->SetName(klass->GetName());
    type->SetResolved(true);
    return true;
  }

  LibraryClass* lib_klass = linker->SearchClassLibraries(type->GetName(), program->GetUses());
  if(lib_klass) {
    lib_klass->SetCalled(true);
    type->SetName(lib_klass->GetName());
    type->SetResolved(true);
    return true;
  }

  // generics
  if(context_klass->HasGenerics()) {
    klass = context_klass->GetGenericClass(type->GetName());
    if(klass) {
      if(klass->HasGenericInterface()) {
        Type* inf_type = klass->GetGenericInterface();
        if(ResolveClassEnumType(inf_type)) {
          type->SetName(inf_type->GetName());
          type->SetResolved(true);
          return true;
        }
      }
      else {
        type->SetName(type->GetName());
        type->SetResolved(true);
        return true;
      }
    }
  }

  Enum* eenum = SearchProgramEnums(type->GetName());
  if(eenum) {
    type->SetName(type->GetName());
    type->SetResolved(true);
    return true;
  }
  else {
    eenum = SearchProgramEnums(context_klass->GetName() + L"#" + type->GetName());
    if(eenum) {
      type->SetName(context_klass->GetName() + L"#" + type->GetName());
      type->SetResolved(true);
      return true;
    }
  }

  LibraryEnum* lib_eenum = linker->SearchEnumLibraries(type->GetName(), program->GetUses());
  if(lib_eenum) {
    type->SetName(lib_eenum->GetName());
    type->SetResolved(true);
    return true;
  }
  else {
    lib_eenum = linker->SearchEnumLibraries(type->GetName(), program->GetUses());
    if(lib_eenum) {
      type->SetName(lib_eenum->GetName());
      type->SetResolved(true);
      return true;
    }
  }

  return false;
}

bool ContextAnalyzer::IsClassEnumParameterMatch(Type* calling_type, Type* method_type)
{
  const wstring& from_klass_name = calling_type->GetName();

  LibraryClass* from_lib_klass = nullptr;
  Class* from_klass = SearchProgramClasses(from_klass_name);
  if(!from_klass && current_class->HasGenerics()) {
    from_klass = current_class->GetGenericClass(from_klass_name);
  }

  if(!from_klass) {
    from_lib_klass = linker->SearchClassLibraries(from_klass_name, program->GetUses());
  }

  // resolve to class name
  wstring to_klass_name;
  Class* to_klass = SearchProgramClasses(method_type->GetName());
  if(!to_klass && current_class->HasGenerics()) {
    to_klass = current_class->GetGenericClass(method_type->GetName());
    if(to_klass) {
      to_klass_name = to_klass->GetName();
    }
  }

  if(!to_klass) {
    LibraryClass* to_lib_klass = linker->SearchClassLibraries(method_type->GetName(), program->GetUses());
    if(to_lib_klass) {
      to_klass_name = to_lib_klass->GetName();
    }
  }

  // check enum types
  if(!from_klass && !from_lib_klass) {
    Enum* from_enum = SearchProgramEnums(from_klass_name);
    LibraryEnum* from_lib_enum = linker->SearchEnumLibraries(from_klass_name, program->GetUses());

    wstring to_enum_name;
    Enum* to_enum = SearchProgramEnums(method_type->GetName());
    if(to_enum) {
      to_enum_name = to_enum->GetName();
    }
    else {
      LibraryEnum* to_lib_enum = linker->SearchEnumLibraries(method_type->GetName(), program->GetUses());
      if(to_lib_enum) {
        to_enum_name = to_lib_enum->GetName();
      }
    }

    // look for exact class match
    if(from_enum && from_enum->GetName() == to_enum_name) {
      return true;
    }

    // look for exact class library match
    if(from_lib_enum && from_lib_enum->GetName() == to_enum_name) {
      return true;
    }
  }
  else {
    // look for exact class match
    if(from_klass && from_klass->GetName() == to_klass_name) {
      return true;
    }

    // look for exact class library match
    if(from_lib_klass && from_lib_klass->GetName() == to_klass_name) {
      return true;
    }
  }

  return false;
}

void ContextAnalyzer::ResolveEnumCall(LibraryEnum* lib_eenum, const wstring& item_name, MethodCall* method_call)
{
  // item_name = method_call->GetMethodCall()->GetVariableName();
  LibraryEnumItem* lib_item = lib_eenum->GetItem(item_name);
  if(lib_item) {
    if(method_call->GetMethodCall()) {
      method_call->GetMethodCall()->SetLibraryEnumItem(lib_item, lib_eenum->GetName());
      method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_eenum->GetName()), false);
      method_call->GetMethodCall()->SetEvalType(method_call->GetEvalType(), false);
    }
    else {
      method_call->SetLibraryEnumItem(lib_item, lib_eenum->GetName());
      method_call->SetEvalType(TypeFactory::Instance()->MakeType(CLASS_TYPE, lib_eenum->GetName()), false);
    }
  }
  else {
    ProcessError(static_cast<Expression*>(method_call), L"Undefined enum item: '" + item_name + L"'");
  }
}

void ContextAnalyzer::AnalyzeCharacterStringVariable(SymbolEntry* entry, CharacterString* char_str, int depth)
{
#ifdef _DEBUG
  Debug(L"variable=|" + entry->GetName() + L"|", char_str->GetLineNumber(), depth + 1);
#endif
  if(!entry->GetType() || entry->GetType()->GetDimension() > 0) {
    ProcessError(char_str, L"Invalid function variable type or dimension size");
  }
  else if(entry->GetType()->GetType() == CLASS_TYPE &&
          entry->GetType()->GetName() != L"System.String" &&
          entry->GetType()->GetName() != L"String") {
    const wstring cls_name = entry->GetType()->GetName();
    Class* klass = SearchProgramClasses(cls_name);
    if(klass) {
      Method* method = klass->GetMethod(cls_name + L":ToString:");
      if(method && method->GetMethodType() != PRIVATE_METHOD) {
        char_str->AddSegment(entry, method);
      }
      else {
        ProcessError(char_str, L"Class/enum variable does not have a public 'ToString' method");
      }
    }
    else {
      LibraryClass* lib_klass = linker->SearchClassLibraries(cls_name, program->GetUses());
      if(lib_klass) {
        LibraryMethod* lib_method = lib_klass->GetMethod(cls_name + L":ToString:");
        if(lib_method && lib_method->GetMethodType() != PRIVATE_METHOD) {
          char_str->AddSegment(entry, lib_method);
        }
        else {
          ProcessError(char_str, L"Class/enum variable does not have a public 'ToString' method");
        }
      }
      else {
        ProcessError(char_str, L"Class/enum variable does not have a 'ToString' method");
      }
    }
  }
  else if(entry->GetType()->GetType() == FUNC_TYPE) {
    ProcessError(char_str, L"Invalid function variable type");
  }
  else {
    char_str->AddSegment(entry);
  }
}

void ContextAnalyzer::AnalyzeVariableCast(Type* to_type, Expression* expression)
{
  if(to_type && to_type->GetType() == CLASS_TYPE && expression->GetCastType() && to_type->GetDimension() < 1 &&
     to_type->GetName() != L"System.Base" && to_type->GetName() != L"Base") {
    const wstring to_class_name = to_type->GetName();
    if(SearchProgramEnums(to_class_name) ||
       linker->SearchEnumLibraries(to_class_name, program->GetUses(current_class->GetFileName()))) {
      return;
    }

    Class* to_class = SearchProgramClasses(to_class_name);
    if(to_class) {
      expression->SetToClass(to_class);
    }
    else {
      LibraryClass* to_lib_class = linker->SearchClassLibraries(to_class_name, program->GetUses());
      if(to_lib_class) {
        expression->SetToLibraryClass(to_lib_class);
      }
      else {
        ProcessError(expression, L"Undefined class: '" + to_class_name + L"'");
      }
    }
  }
}

void ContextAnalyzer::AnalyzeVariableFunctionParameters(Type* func_type, ParseNode* node, Class* klass)
{
  const vector<Type*> func_params = func_type->GetFunctionParameters();
  Type* rtrn_type = func_type->GetFunctionReturn();

  // might be a resolved string from a class library
  if(func_params.size() > 0 && rtrn_type) {
    for(size_t i = 0; i < func_params.size(); ++i) {
      Type* type = func_params[i];
      if(type->GetType() == CLASS_TYPE && !ResolveClassEnumType(type, klass)) {
        ProcessError(node, L"Undefined class or enum: '" + type->GetName() + L"'");
      }
    }

    if(rtrn_type && rtrn_type->GetType() == CLASS_TYPE && !ResolveClassEnumType(rtrn_type, klass)) {
      ProcessError(node, L"Undefined class or enum: '" + rtrn_type->GetName() + L"'");
    }
  }
}

void ContextAnalyzer::AddMethodParameter(MethodCall* method_call, SymbolEntry* entry, int depth)
{
  const wstring& entry_name = entry->GetName();
  const size_t start = entry_name.rfind(':');
  if(start != wstring::npos) {
    const wstring& param_name = entry_name.substr(start + 1);
    Variable * variable = TreeFactory::Instance()->MakeVariable(static_cast<Expression*>(method_call)->GetFileName(),
                                                                static_cast<Expression*>(method_call)->GetLineNumber(),
                                                                param_name);
    method_call->SetVariable(variable);
    AnalyzeVariable(variable, entry, depth + 1);
  }
}

bool ContextAnalyzer::ClassEquals(const wstring &left_name, Class* right_klass, LibraryClass* right_lib_klass)
{
  Class* left_klass = nullptr; LibraryClass* left_lib_klass = nullptr;
  if(GetProgramLibraryClass(left_name, left_klass, left_lib_klass)) {
    if(left_klass && right_klass) {
      return left_klass->GetName() == right_klass->GetName();
    }
    else if(left_lib_klass && right_lib_klass) {
      return left_lib_klass->GetName() == right_lib_klass->GetName();
    }
  }

  if(right_klass) {
    left_klass = current_class->GetGenericClass(left_name);
    if(left_klass) {
      return left_klass->GetName() == right_klass->GetName();
    }
  }

  return false;
}

Type* ContextAnalyzer::ResolveGenericType(Type* candidate_type, MethodCall* method_call, Class* klass, LibraryClass* lib_klass, bool is_rtrn)
{
  const bool has_generics = (klass && klass->HasGenerics()) || (lib_klass && lib_klass->HasGenerics());
  if(has_generics) {
    if(candidate_type->GetType() == FUNC_TYPE) {
      if(klass) {
        Type* concrete_rtrn = ResolveGenericType(candidate_type->GetFunctionReturn(), method_call, klass, lib_klass, false);
        vector<Type*> concrete_params;
        const vector<Type*> type_params = candidate_type->GetFunctionParameters();
        for(size_t i = 0; i < type_params.size(); ++i) {
          concrete_params.push_back(ResolveGenericType(type_params[i], method_call, klass, lib_klass, false));
        }

        return TypeFactory::Instance()->MakeType(concrete_params, concrete_rtrn);
      }
      else {
        ResolveClassEnumType(candidate_type);
        wstring func_name = candidate_type->GetName();

        const vector<LibraryClass*> generic_classes = lib_klass->GetGenericClasses();
        for(size_t i = 0; i < generic_classes.size(); ++i) {
          const wstring find_name = generic_classes[i]->GetName();
          Type* to_type = ResolveGenericType(TypeFactory::Instance()->MakeType(CLASS_TYPE, find_name), method_call, klass, lib_klass, false);
          const wstring from_name = L"o." + generic_classes[i]->GetName();
          const wstring to_name = L"o." + to_type->GetName();
          ReplaceAllSubstrings(func_name, from_name, to_name);
        }

        return TypeParser::ParseType(func_name);
      }
    }
    else {
      // find concrete index
      int concrete_index = -1;
      ResolveClassEnumType(candidate_type);
      const wstring generic_name = candidate_type->GetName();
      if(klass) {
        concrete_index = klass->GenericIndex(generic_name);
      }
      else if(lib_klass) {
        concrete_index = lib_klass->GenericIndex(generic_name);
      }

      if(is_rtrn) {
        Class* klass_generic = nullptr; LibraryClass* lib_klass_generic = nullptr;
        if(GetProgramLibraryClass(candidate_type, klass_generic, lib_klass_generic)) {
          const vector<Type*> candidate_types = GetConcreteTypes(method_call);
          if(method_call->GetEntry()) {
            const vector<Type*> concrete_types = method_call->GetEntry()->GetType()->GetGenerics();
            for(size_t i = 0; i < candidate_types.size(); ++i) {
              if(klass && method_call->GetEvalType()) {
                const vector<Type*> map_types = GetMethodCallGenerics(method_call);
                if(i < map_types.size()) {
                  ResolveClassEnumType(map_types[i]);
                }
                else {
                  ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
                }
              }
              else if(lib_klass && method_call->GetEvalType()) {
                const vector<Type*> map_types = GetMethodCallGenerics(method_call);
                if(i < map_types.size()) {
                  Type* map_type = map_types[i];
                  ResolveClassEnumType(map_type);

                  const int map_type_index = lib_klass->GenericIndex(map_type->GetName());
                  if(map_type_index > -1 && map_type_index < (int)concrete_types.size()) {
                    Type* candidate_type = candidate_types[i];
                    ResolveClassEnumType(candidate_type);
                    
                    Type* concrete_type = concrete_types[map_type_index];
                    ResolveClassEnumType(concrete_type);

                    if(candidate_type->GetName() != concrete_type->GetName()) {
                      ProcessError(static_cast<Expression*>(method_call), L"Invalid generic to concrete type mismatch '" +
                                   concrete_type->GetName() + L"' to '" + candidate_type->GetName() + L"'");
                    }
                  }
                  else {
                    const vector<Type*> from_concrete_types = concrete_types;
                    const vector<Type*> to_concrete_types = GetMethodCallGenerics(method_call);
                    if(from_concrete_types.size() == to_concrete_types.size()) {
                      for(size_t j = 0; j < from_concrete_types.size(); ++j) {
                        Type* from_concrete_type = from_concrete_types[j];
                        Type* to_concrete_type = to_concrete_types[j];
                        if(from_concrete_type->GetName() != to_concrete_type->GetName()) {
                          ProcessError(static_cast<Expression*>(method_call), L"Invalid generic to concrete type mismatch '" +
                                       from_concrete_type->GetName() + L"' to '" + to_concrete_type->GetName() + L"'");
                        }
                      }
                    }
                    else {
                      ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
                    }
                  }
                }
                else {
                  ProcessError(static_cast<Expression*>(method_call), L"Concrete to generic size mismatch");
                }
              }
            }
          }
          
          if(klass_generic && klass_generic->HasGenerics()) {
            ValidateGenericConcreteMapping(candidate_types, klass_generic, static_cast<Expression*>(method_call));
            if(method_call->GetEvalType()) {
              method_call->GetEvalType()->SetGenerics(candidate_types);
            }
          }
          else if(lib_klass_generic && lib_klass_generic->HasGenerics()) {
            ValidateGenericConcreteMapping(candidate_types, lib_klass_generic, static_cast<Expression*>(method_call));
            if(method_call->GetEvalType()) {
              method_call->GetEvalType()->SetGenerics(candidate_types);
            }
          }
        }
      }

      // find concrete type
      if(concrete_index > -1) {
        vector<Type*> concrete_types;
        // get types from declaration
        if(method_call->GetEntry()) {
          concrete_types = method_call->GetEntry()->GetType()->GetGenerics();
        }
        else if(method_call->GetVariable() && method_call->GetVariable()->GetEntry()) {
          concrete_types = method_call->GetVariable()->GetEntry()->GetType()->GetGenerics();
        }
        else if(method_call->GetCallType() == NEW_INST_CALL) {
          concrete_types = GetConcreteTypes(method_call);
        }
        else if(method_call->GetEvalType()) {
          Expression* prev_call = method_call;
          while(prev_call->GetPreviousExpression()) {
            prev_call = prev_call->GetPreviousExpression();
          }

          if(prev_call->GetExpressionType() == METHOD_CALL_EXPR) {
            MethodCall* first_call = static_cast<MethodCall*>(prev_call);
            concrete_types = first_call->GetEntry()->GetType()->GetGenerics();
            while(concrete_types.size() == 1 && concrete_types.front()->GetGenerics().size()) {
              concrete_types = concrete_types.front()->GetGenerics();
            }
          }
        }

        // get concrete type
        if(concrete_index < (int)concrete_types.size()) {
          Type* concrete_type = TypeFactory::Instance()->MakeType(concrete_types[concrete_index]);
          concrete_type->SetDimension(candidate_type->GetDimension());
          ResolveClassEnumType(concrete_type);
          return concrete_type;
        }
      }
    }
  }

  return candidate_type;
}

Type* ContextAnalyzer::ResolveGenericType(Type* type, Expression* expression, Class* left_klass, LibraryClass* lib_left_klass)
{
  int concrete_index = -1;
  const wstring left_type_name = type->GetName();

  if(left_klass) {
    concrete_index = left_klass->GenericIndex(left_type_name);
  }
  else if(lib_left_klass) {
    concrete_index = lib_left_klass->GenericIndex(left_type_name);
  }

  if(concrete_index > -1) {
    vector<Type*> concrete_types;

    if(expression->GetExpressionType() == VAR_EXPR) {
      Variable* variable = static_cast<Variable*>(expression);
      if(variable->GetEntry()) {
        concrete_types = variable->GetEntry()->GetType()->GetGenerics();
      }
    }
    else if(expression->GetExpressionType() == METHOD_CALL_EXPR) {
      concrete_types = GetConcreteTypes(static_cast<MethodCall*>(expression));
    }

    if(concrete_index < (int)concrete_types.size()) {
      return concrete_types[concrete_index];
    }
  }

  return type;
}

Class* ContextAnalyzer::SearchProgramClasses(const wstring& klass_name)
{
  Class* klass = program->GetClass(klass_name);
  if(!klass) {
    klass = program->GetClass(bundle->GetName() + L"." + klass_name);
    if(!klass) {
      vector<wstring> uses = program->GetUses();
      for(size_t i = 0; !klass && i < uses.size(); ++i) {
        klass = program->GetClass(uses[i] + L"." + klass_name);
      }
    }
  }

  return klass;
}

Enum* ContextAnalyzer::SearchProgramEnums(const wstring& eenum_name)
{
  Enum* eenum = program->GetEnum(eenum_name);
  if(!eenum) {
    eenum = program->GetEnum(bundle->GetName() + L"." + eenum_name);
    if(!eenum) {
      vector<wstring> uses = program->GetUses();
      for(size_t i = 0; !eenum && i < uses.size(); ++i) {
        eenum = program->GetEnum(uses[i] + L"." + eenum_name);
        if(!eenum) {
          eenum = program->GetEnum(uses[i] + eenum_name);
        }
      }
    }
  }

  return eenum;
}

/****************************
 * Support for inferred method
 * signatures
 ****************************/
LibraryMethod* LibraryMethodCallSelector::GetSelection()
{
  // no match
  if(valid_matches.size() == 0) {
    return nullptr;
  }
  // single match
  else if(valid_matches.size() == 1) {
    method_call->GetCallingParameters()->SetExpressions(valid_matches.front()->GetCallingParameters());
    return valid_matches.front()->GetLibraryMethod();
  }

  int match_index = -1;
  int high_score = 0;
  for(size_t i = 0; i < matches.size(); ++i) {
    // calculate match score
    int match_score = 0;
    bool exact_match = true;
    vector<int> parm_matches = matches[i]->GetParameterMatches();
    for(size_t j = 0; exact_match && j < parm_matches.size(); ++j) {
      if(parm_matches[j] == 0) {
        match_score++;
      }
      else {
        exact_match = false;
      }
    }
    // save the index of the best match
    if(match_score > high_score) {
      match_index = (int)i;
      high_score = match_score;
    }
  }

  if(match_index == -1) {
    return nullptr;
  }

  method_call->GetCallingParameters()->SetExpressions(matches[match_index]->GetCallingParameters());
  return matches[match_index]->GetLibraryMethod();
}

Method* MethodCallSelector::GetSelection()
{
  // no match
  if(valid_matches.size() == 0) {
    return nullptr;
  }
  // single match
  else if(valid_matches.size() == 1) {
    method_call->GetCallingParameters()->SetExpressions(valid_matches.front()->GetCallingParameters());
    return valid_matches.front()->GetMethod();
  }

  int match_index = -1;
  int high_score = 0;
  for(size_t i = 0; i < matches.size(); ++i) {
    // calculate match score
    int match_score = 0;
    bool exact_match = true;
    vector<int> parm_matches = matches[i]->GetParameterMatches();
    for(size_t j = 0; exact_match && j < parm_matches.size(); ++j) {
      if(parm_matches[j] == 0) {
        match_score++;
      }
      else {
        exact_match = false;
      }
    }
    // save the index of the best match
    if(match_score > high_score) {
      match_index = (int)i;
      high_score = match_score;
    }
  }

  if(match_index == -1) {
    return nullptr;
  }

  method_call->GetCallingParameters()->SetExpressions(matches[match_index]->GetCallingParameters());
  return matches[match_index]->GetMethod();
}

vector<Type*> ContextAnalyzer::GetMethodCallGenerics(MethodCall* method_call)
{
  vector<Type*> concrete_types;

  Expression* prev_call = method_call;
  while(prev_call->GetPreviousExpression()) {
    prev_call = prev_call->GetPreviousExpression();
  }

  if(prev_call->GetExpressionType() == METHOD_CALL_EXPR) {
    MethodCall* first_call = static_cast<MethodCall*>(prev_call);
    concrete_types = first_call->GetEntry()->GetType()->GetGenerics();
    while(concrete_types.size() == 1 && concrete_types.front()->GetGenerics().size()) {
      concrete_types = concrete_types.front()->GetGenerics();
    }
  }

  return concrete_types;
}
