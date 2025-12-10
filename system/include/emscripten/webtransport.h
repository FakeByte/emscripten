
#pragma once

//#include <stdint.h>
//#include <memory.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns true if WebTransport is supported by the current browser
bool emscripten_webtransport_is_supported(void);

#ifdef __cplusplus
} // ~extern "C"
#endif