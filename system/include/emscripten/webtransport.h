
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

// WebTransport ready state constants
#define EMSCRIPTEN_WEBTRANSPORT_STATE_CONNECTING 0
#define EMSCRIPTEN_WEBTRANSPORT_STATE_CONNECTED 1
#define EMSCRIPTEN_WEBTRANSPORT_STATE_CLOSED 2
#define EMSCRIPTEN_WEBTRANSPORT_STATE_FAILED 3

// Attributes structure for creating a WebTransport connection
typedef struct EmscriptenWebTransportCreateAttributes {
  // The target URL to connect to. This string can point to a stack local variable, the string is read immediately at a call to emscripten_webtransport_new().
  const char *url;

  // If true, allows the user agent to pool this connection with others.
  bool allowPooling;

  // Congestion control algorithm to use. Can be "default", "throughput", or "low-latency".
  // This string can point to a stack local variable, the string is read immediately at a call to emscripten_webtransport_new().
  const char *congestionControl;

  // If true, requires the connection to use unreliable transport.
  bool requireUnreliable;
} EmscriptenWebTransportCreateAttributes;

// Initialize create attributes with default values
#define emscripten_webtransport_init_create_attributes(attributes) do { memset((attributes), 0, sizeof(EmscriptenWebTransportCreateAttributes)); } while(0)

// Returns true if WebTransport is supported by the current browser
bool emscripten_webtransport_is_supported(void);

// Creates a new WebTransport and connects it to the given URL.
// If the return value of this function is > 0, the function has succeeded and the return value represents a handle to the WebTransport object.
// If the return value of this function is < 0, then the function has failed, and the return value can be interpreted as a EMSCRIPTEN_RESULT code
// representing the cause of the failure. If the function returns 0, then the call has failed with an unknown reason (build with -sWEBTRANSPORT_DEBUG for more information)
EMSCRIPTEN_WEBTRANSPORT_T emscripten_webtransport_new(EmscriptenWebTransportCreateAttributes *createAttributes __attribute__((nonnull)));

// Returns the current ready state of the WebTransport connection into readyState. readyState must not be a null pointer.
// Returns one of EMSCRIPTEN_WEBTRANSPORT_STATE_* constants.
EMSCRIPTEN_RESULT emscripten_webtransport_get_ready_state(EMSCRIPTEN_WEBTRANSPORT_T transport, unsigned short *readyState __attribute__((nonnull)));

// Writes the WebTransport.url field as a UTF-8 string to the memory area pointed by url. The memory area must contain at least urlLength bytes of free space.
// If this memory area cannot fit the url string, it will be truncated. Call emscripten_webtransport_get_url_length() to determine how large memory area will be required to store the url.
// url must not be a null pointer.
EMSCRIPTEN_RESULT emscripten_webtransport_get_url(EMSCRIPTEN_WEBTRANSPORT_T transport, char *url __attribute__((nonnull)), int urlLength);

// Returns the byte length needed to store WebTransport.url string in Wasm heap. This length can be passed to emscripten_webtransport_get_url as it includes the null byte in the count.
// urlLength must not be a null pointer.
EMSCRIPTEN_RESULT emscripten_webtransport_get_url_length(EMSCRIPTEN_WEBTRANSPORT_T transport, int *urlLength __attribute__((nonnull)));

// Closes the specified WebTransport connection.
// closeCode: A code indicating the reason for closing. If 0, no close code is sent.
// reason: A string describing the reason for closing. Can be NULL.
EMSCRIPTEN_RESULT emscripten_webtransport_close(EMSCRIPTEN_WEBTRANSPORT_T transport, unsigned short closeCode, const char *reason);

// Returns the close code from the closed promise into closeCode. closeCode must not be a null pointer.
// This is only valid after the connection has entered CLOSED or FAILED state.
EMSCRIPTEN_RESULT emscripten_webtransport_get_close_code(EMSCRIPTEN_WEBTRANSPORT_T transport, unsigned short *closeCode __attribute__((nonnull)));

// Writes the close reason from the closed promise as a UTF-8 string to the memory area pointed by reason.
// This is only valid after the connection has entered CLOSED or FAILED state.
// reason must not be a null pointer.
EMSCRIPTEN_RESULT emscripten_webtransport_get_close_reason(EMSCRIPTEN_WEBTRANSPORT_T transport, char *reason __attribute__((nonnull)), int reasonLength);

// Returns the byte length needed to store the close reason string in Wasm heap.
// reasonLength must not be a null pointer.
EMSCRIPTEN_RESULT emscripten_webtransport_get_close_reason_length(EMSCRIPTEN_WEBTRANSPORT_T transport, int *reasonLength __attribute__((nonnull)));

// Releases the given WebTransport object and all associated allocated memory for garbage collection.
// This effectively frees the transport handle, after calling this function the given handle no longer exists.
EMSCRIPTEN_RESULT emscripten_webtransport_delete(EMSCRIPTEN_WEBTRANSPORT_T transport);

// This function close()s and releases all created WebTransport connections for the current thread.
// You can call this at application exit time to enforce teardown of all active connections.
void emscripten_webtransport_deinitialize(void);

#ifdef __cplusplus
} // ~extern "C"
#endif