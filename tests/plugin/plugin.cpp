// A shared library for the dylib test to load.
#if defined(_WIN32)
#  define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#  define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

PLUGIN_EXPORT int oat_test_plugin_add(int a, int b) { return a + b; }
