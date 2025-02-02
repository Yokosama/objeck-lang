/***************************************************************************
* REPL shell
*
* Copyright (c) 2023, Randy Hollines
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
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
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

#include "repl.h"
#include <codecvt>

/****************************
* Program start
****************************/
int main(int argc, char* argv[])
{
#ifndef _MSYS2_CLANG
  SetEnv();
#else
  std::ios_base::sync_with_stdio(false);
  std::locale utf8(std::locale(), new std::codecvt_utf8_utf16<wchar_t>);
  std::wcout.imbue(utf8);
  std::wcin.imbue(utf8);
#endif

#ifdef _DEBUG
  OpenLogger("debug.log");
#endif

  // parse command line
  std::wstring cmd_line;
  std::map<const std::wstring, std::wstring> arguments = ParseCommnadLine(argc, argv, cmd_line);
  
  std::wstring input;
  std::wstring libs;
  std::wstring opt;
  int mode = 0;
  bool is_exit = false;

  auto result = arguments.find(FILE_PARAM);
  if(result != arguments.end()) {
    input = result->second;
    mode = 1;
    arguments.erase(FILE_PARAM);
  }

  result = arguments.find(INLINE_PARAM);
  if(result != arguments.end()) {
    input = result->second;
    mode = 2;
    arguments.erase(INLINE_PARAM);
  }

  result = arguments.find(EXIT_PARAM);
  if(result != arguments.end()) {
    is_exit = true;
    arguments.erase(EXIT_PARAM);
  }

  result = arguments.find(LIBS_PARAM);
  if(result != arguments.end()) {
    libs = result->second;
    arguments.erase(LIBS_PARAM);
  }

  result = arguments.find(OPT_PARAM);
  if(result != arguments.end()) {
    opt = result->second;
    arguments.erase(OPT_PARAM);
  }

  // input check
  if(arguments.size()) {
    Usage();
  }
  // start repl loop
  else {
    Editor editor;
    editor.Edit(input, libs, opt, mode, is_exit);
  }

#ifdef _DEBUG
  CloseLogger();
#endif
}

void Usage()
{
  std::wcerr << L"Usage: obi" << std::endl << std::endl;
  std::wcerr << std::endl << L"Options:" << std::endl;
  std::wcerr << L"  -file: [optional] source file" << std::endl;
  std::wcerr << L"  -inline: [optional] inline source code" << std::endl;
  std::wcerr << L"  -lib: [optional] list of linked libraries (separated by commas)" << std::endl;
  std::wcerr << L"  -opt: [optional] compiler optimizations s0-s3 (s3 being the most aggressive and default)" << std::endl;
  std::wcerr << L"  -help: [optional] comand line options" << std::endl;
  std::wcerr << L"  -exit: [optional] shell will exit after command-line execution" << std::endl;

  std::wcout << std::endl << L"---" << std::endl; 

#if defined(_WIN64) && defined(_WIN32)
  std::wcout << VERSION_STRING << L" Objeck (Windows x86_64)" << std::endl;
#elif _WIN32
  std::wcout << VERSION_STRING << L" Objeck (Windows x86)" << std::endl;
#elif _OSX
#ifdef _ARM64
  std::wcout << VERSION_STRING << L" Objeck (macOS ARM64)" << std::endl;
#else
  std::wcout << VERSION_STRING << L" Objeck (macOS x86_64)" << std::endl;
#endif
#elif _X64
  std::wcout << VERSION_STRING << L" Objeck (Linux x86_64)" << std::endl;
#elif _ARM32
  std::wcout << VERSION_STRING << L" Objeck (Linux ARMv7)" << std::endl;
#else
  std::wcout << VERSION_STRING << L" Objeck (Linux x86)" << std::endl;
#endif

  std::wcout << std::endl << L"Copyright (c) 2023, Randy Hollines" << std::endl;
  std::wcout << L"This is free software; see the source for copying conditions.There is NO" << std::endl;
  std::wcout << L"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE." << std::endl;
}

void SetEnv() {
#ifdef _WIN32
#ifdef _MSYS2_CLANG
  std::ios_base::sync_with_stdio(false);
  std::locale utf8(std::locale(), new std::codecvt_utf8_utf16<wchar_t>);
  std::wcout.imbue(utf8);
  std::wcin.imbue(utf8);
#else
  if(_setmode(_fileno(stdin), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }

  if(_setmode(_fileno(stdout), _O_U8TEXT) < 0) {
    std::wcerr << "Unable to initialize I/O subsystem" << std::endl;
    exit(1);
  }
#endif
#else
#if defined(_X64)
  char* locale = setlocale(LC_ALL, "");
  std::locale lollocale(locale);
  std::setlocale(LC_ALL, locale);
  std::wcout.imbue(lollocale);
#elif defined(_ARM64)
  char* locale = setlocale(LC_ALL, "");
  std::locale lollocale(locale);
  std::setlocale(LC_ALL, locale);
  std::wcout.imbue(lollocale);
  std::setlocale(LC_ALL, "en_US.utf8");
#else    
  setlocale(LC_ALL, "en_US.utf8");
#endif
#endif
}
