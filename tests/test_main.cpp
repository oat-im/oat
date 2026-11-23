#include "test.h"

#if defined(_WIN32)
#  include <crtdbg.h>
#  include <cstdlib>
extern "C" __declspec(dllimport) unsigned int __stdcall SetErrorMode(unsigned int mode);

// On Windows a failed library load, a crash, or abort() can open a modal
// dialog. On a build machine nobody is there to click it, so the run hangs.
// Send all of them to stderr instead.
static void no_dialogs() {
  SetErrorMode(0x0001 | 0x0002 | 0x8000); // FAILCRITICALERRORS | NOGPFAULTERRORBOX | NOOPENFILEERRORBOX
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  for (int kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(kind, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
  }
}
#else
static void no_dialogs() {}
#endif

int main(int argc, char** argv) {
  no_dialogs();
  return oat_test::run_all(argc, argv);
}
