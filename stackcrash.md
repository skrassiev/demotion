✦ I have updated the project to include a robust stack crash dump mechanism that filters line numbers to only show
  for main.cc, satisfying the requirements for both amd64 and aarch64 builds.

  Key Changes:
   1. Enhanced Crash Handling:
       * Replaced std::abort() in main() with a custom crash reporter that prints the error message and immediately
         invokes the stack dump.
       * Added std::set_terminate to catch unhandled exceptions (runtime errors) and print a stack trace before
         exiting.
       * Implemented sigaction-based signal handling for SIGSEGV, SIGABRT, SIGILL, and SIGFPE for more reliable
         crash reporting.

   2. Surgical Stack Dump:
       * Uses backtrace and addr2line to generate the trace.
       * Line Number Filtering: Implemented logic to show full file/line information only for frames originating
         from main.cc. For all other frames, it strips the file/line info, showing only the function name or
         symbol.
       * Used [[gnu::noinline]] to ensure the dump function itself doesn't obscure the top of the stack.

   3. Build Configuration:
       * Updated BUILD with -fno-omit-frame-pointer and -fno-optimize-sibling-calls to ensure reliable frame
         traversal on both amd64 and aarch64.
       * Kept -rdynamic and -g to ensure symbols are available for addr2line.

  These changes ensure that any fatal runtime error or crash provides a clean, focused stack trace that highlights
  relevant code in main.cc while keeping the rest of the trace concise.
● YOLO                                                                                    press tab twice for more

