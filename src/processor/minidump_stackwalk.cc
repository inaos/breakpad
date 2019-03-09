// Copyright (c) 2010 Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// minidump_stackwalk.cc: Process a minidump with MinidumpProcessor, printing
// the results, including stack traces.
//
// Author: Mark Mentovai

#include <stdio.h>
#include <string.h>

#ifndef WIN32
#include <unistd.h>
#endif

#include <limits>
#include <string>
#include <vector>

#include "common/path_helper.h"
#include "common/scoped_ptr.h"
#include "common/using_std_string.h"
#include "google_breakpad/processor/basic_source_line_resolver.h"
#include "google_breakpad/processor/minidump.h"
#include "google_breakpad/processor/minidump_processor.h"
#include "google_breakpad/processor/process_state.h"
#include "processor/logging.h"
#include "processor/simple_symbol_supplier.h"
#include "processor/stackwalk_common.h"

#ifdef WIN32
#include <string.h>
#include <stdio.h>

int     opterr = 1,             /* if error message should be printed */
        optind = 1,             /* index into parent argv vector */
        optopt,                 /* character checked for validity */
        optreset;               /* reset getopt */
char    *optarg;                /* argument associated with option */

#define BADCH   (int)'?'
#define BADARG  (int)':'
#define EMSG    ""

/*
 * getopt --
 *      Parse argc/argv argument vector.
 */
int getopt(int nargc, char * const nargv[], const char *ostr)
{
  static char *place = EMSG;              /* option letter processing */
  const char *oli;                              /* option letter list index */

  if (optreset || !*place) {              /* update scanning pointer */
    optreset = 0;
    if (optind >= nargc || *(place = nargv[optind]) != '-') {
      place = EMSG;
      return (-1);
    }
    if (place[1] && *++place == '-') {      /* found "--" */
      ++optind;
      place = EMSG;
      return (-1);
    }
  }                                       /* option letter okay? */
  if ((optopt = (int)*place++) == (int)':' ||
    !(oli = strchr(ostr, optopt))) {
      /*
      * if the user didn't specify '-' as an option,
      * assume it means -1.
      */
      if (optopt == (int)'-')
        return (-1);
      if (!*place)
        ++optind;
      if (opterr && *ostr != ':')
        (void)printf("illegal option -- %c\n", optopt);
      return (BADCH);
  }
  if (*++oli != ':') {                    /* don't need argument */
    optarg = NULL;
    if (!*place)
      ++optind;
  }
  else {                                  /* need an argument */
    if (*place)                     /* no white space */
      optarg = place;
    else if (nargc <= ++optind) {   /* no arg */
      place = EMSG;
      if (*ostr == ':')
        return (BADARG);
      if (opterr)
        (void)printf("option requires an argument -- %c\n", optopt);
      return (BADCH);
    }
    else                            /* white space */
      optarg = nargv[optind];
    place = EMSG;
    ++optind;
  }
  return (optopt);                        /* dump back option letter */
}
#endif

namespace {

struct Options {
  bool machine_readable;
  bool output_stack_contents;

  string minidump_file;
  std::vector<string> symbol_paths;
};

using google_breakpad::BasicSourceLineResolver;
using google_breakpad::Minidump;
using google_breakpad::MinidumpMemoryList;
using google_breakpad::MinidumpThreadList;
using google_breakpad::MinidumpProcessor;
using google_breakpad::ProcessState;
using google_breakpad::SimpleSymbolSupplier;
using google_breakpad::scoped_ptr;

// Processes |options.minidump_file| using MinidumpProcessor.
// |options.symbol_path|, if non-empty, is the base directory of a
// symbol storage area, laid out in the format required by
// SimpleSymbolSupplier.  If such a storage area is specified, it is
// made available for use by the MinidumpProcessor.
//
// Returns the value of MinidumpProcessor::Process.  If processing succeeds,
// prints identifying OS and CPU information from the minidump, crash
// information if the minidump was produced as a result of a crash, and
// call stacks for each thread contained in the minidump.  All information
// is printed to stdout.
bool PrintMinidumpProcess(const Options& options) {
  scoped_ptr<SimpleSymbolSupplier> symbol_supplier;
  if (!options.symbol_paths.empty()) {
    // TODO(mmentovai): check existence of symbol_path if specified?
    symbol_supplier.reset(new SimpleSymbolSupplier(options.symbol_paths));
  }

  BasicSourceLineResolver resolver;
  MinidumpProcessor minidump_processor(symbol_supplier.get(), &resolver);

  // Increase the maximum number of threads and regions.
  MinidumpThreadList::set_max_threads(std::numeric_limits<uint32_t>::max());
  MinidumpMemoryList::set_max_regions(std::numeric_limits<uint32_t>::max());
  // Process the minidump.
  Minidump dump(options.minidump_file);
  if (!dump.Read()) {
     BPLOG(ERROR) << "Minidump " << dump.path() << " could not be read";
     return false;
  }
  ProcessState process_state;
  if (minidump_processor.Process(&dump, &process_state) !=
      google_breakpad::PROCESS_OK) {
    BPLOG(ERROR) << "MinidumpProcessor::Process failed";
    return false;
  }

  if (options.machine_readable) {
    PrintProcessStateMachineReadable(process_state);
  } else {
    PrintProcessState(process_state, options.output_stack_contents, &resolver);
  }

  return true;
}

}  // namespace

static void Usage(int argc, const char *argv[], bool error) {
  std::string bn(argv[0]);
  fprintf(error ? stderr : stdout,
          "Usage: %s [options] <minidump-file> [symbol-path ...]\n"
          "\n"
          "Output a stack trace for the provided minidump\n"
          "\n"
          "Options:\n"
          "\n"
          "  -m         Output in machine-readable format\n"
          "  -s         Output stack contents\n",
          bn.c_str());
}

static void SetupOptions(int argc, const char *argv[], Options* options) {
  int ch;

  options->machine_readable = false;
  options->output_stack_contents = false;

  while ((ch = getopt(argc, (char * const *)argv, "hms")) != -1) {
    switch (ch) {
      case 'h':
        Usage(argc, argv, false);
        exit(0);
        break;

      case 'm':
        options->machine_readable = true;
        break;
      case 's':
        options->output_stack_contents = true;
        break;

      case '?':
        Usage(argc, argv, true);
        exit(1);
        break;
    }
  }

  if ((argc - optind) == 0) {
    fprintf(stderr, "%s: Missing minidump file\n", argv[0]);
    Usage(argc, argv, true);
    exit(1);
  }

  options->minidump_file = argv[optind];

  for (int argi = optind + 1; argi < argc; ++argi)
    options->symbol_paths.push_back(argv[argi]);
}

int main(int argc, const char* argv[]) {
  Options options;
  SetupOptions(argc, argv, &options);

  return PrintMinidumpProcess(options) ? 0 : 1;
}
