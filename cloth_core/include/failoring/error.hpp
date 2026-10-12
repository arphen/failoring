#pragma once
// Error reporting that also works when exceptions are disabled (WebAssembly build).
#include <stdexcept>
#if defined(__cpp_exceptions)
#define FL_THROW(Type, msg) throw Type(msg)
#else
#define FL_THROW(Type, msg) __builtin_trap()
#endif
