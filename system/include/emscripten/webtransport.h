
#pragma once

#include <stdint.h>
#include <memory.h>

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#ifdef __cplusplus
extern "C" {
#endif

// Handle type for WebTransport connections
#define EMSCRIPTEN_WEBTRANSPORT_T int

typedef struct WebTransportHash {
  const char *algorithm;
  void *value;
  int valueLength;
} WebTransportHash ;

typedef struct WebTransportCreateOptions {
  WebTransportHash *serverCertificateHashes;
  int serverCertificateHashesCount;
} WebTransportCreateOptions;

typedef struct WebTransportCloseInfo {
  int closeCode;
  const char *reason;
} WebTransportCloseInfo;

bool emscripten_webtransport_is_supported(void);

// Creates a new WebTransport and connects it to the given remote host.
// createAttributes can be set to NULL if no options are provided
// If the return value of this function is > 0, the function has succeeded and the return value represents a handle to the WebTransport object.
// If the return value of this function is < 0, then the function has failed, and the return value can be interpreted as a EMSCRIPTEN_RESULT code
// representing the cause of the failure. If the function returns 0, then the call has failed with an unknown reason
EMSCRIPTEN_WEBTRANSPORT_T emscripten_webtransport_new(const char *url, WebTransportCreateOptions *createAttributes);

typedef void (*em_webtransport_ready_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData);
EMSCRIPTEN_RESULT emscripten_webtransport_set_onready_callback(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, em_webtransport_ready_callback_func callback);

// Callback for when the WebTransport connection is closed.
// closeCode: The error code for the closure (0 if not provided).
// reason: The reason string for the closure (may be empty).
typedef void (*em_webtransport_closed_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, int closeCode, const char *reason);
EMSCRIPTEN_RESULT emscripten_webtransport_set_onclosed_callback(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, em_webtransport_closed_callback_func callback);

// Closes the specified WebTransport connection.
// closeInfo: A pointer to a WebTransportCloseInfo struct containing the close code and reason.
// Can be NULL to close without providing close info.
EMSCRIPTEN_RESULT emscripten_webtransport_close(EMSCRIPTEN_WEBTRANSPORT_T transport, WebTransportCloseInfo *closeInfo);

// Releases the given WebTransport object and all associated allocated memory for garbage collection.
// This effectively frees the transport handle, after calling this function the given handle no longer exists.
EMSCRIPTEN_RESULT emscripten_webtransport_delete(EMSCRIPTEN_WEBTRANSPORT_T transport);

// This function close()s and releases all created WebTransport connections for the current thread.
// You can call this at application exit time to enforce teardown of all active connections.
void emscripten_webtransport_deinitialize(void);

#ifdef __cplusplus
} // ~extern "C"
#endif