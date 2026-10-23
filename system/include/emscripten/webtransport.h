/*
 * Copyright 2025 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 */

// C bindings for the WebTransport API:
// https://developer.mozilla.org/en-US/docs/Web/API/WebTransport
//
// Every asynchronous operation of the JS API (the promises) takes a callback
// and a userData pointer. The callback is invoked from the browser event loop
// once the operation finishes, so the calling thread has to yield back to the
// event loop for callbacks to be delivered. If such a function does not return
// EMSCRIPTEN_RESULT_SUCCESS, its callback will never be called.
//
// All pointers passed to a callback (strings, structs) are only valid for the
// duration of the callback.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <emscripten/html5.h>

#ifdef __cplusplus
extern "C" {
#endif

// Handle to a WebTransport object. Valid handles are > 0.
typedef int EMSCRIPTEN_WEBTRANSPORT_T;

// WebTransportError.source
typedef enum WebTransportErrorSource {
  // The error is not a WebTransportError (e.g. an InvalidStateError
  // DOMException or a TypeError), so it has no source.
  WEBTRANSPORT_ERROR_SOURCE_NONE = 0,
  WEBTRANSPORT_ERROR_SOURCE_STREAM = 1,
  WEBTRANSPORT_ERROR_SOURCE_SESSION = 2,
} WebTransportErrorSource;

// The reason an asynchronous operation failed: a WebTransportError, a
// DOMException or any other JS error the browser rejected the promise with.
typedef struct WebTransportError {
  // The error name, e.g. "WebTransportError" or "InvalidStateError".
  const char *name;
  const char *message;
  WebTransportErrorSource source;
  // The application protocol error code (0-255), or -1 if there is none.
  int streamErrorCode;
} WebTransportError;

// An element of WebTransportOptions.serverCertificateHashes.
typedef struct WebTransportHash {
  // The hash algorithm, currently only "sha-256" is specified.
  const char *algorithm;
  const void *value;
  size_t valueLength;
} WebTransportHash;

// Options for emscripten_webtransport_new(). Zero-initialize the struct to
// get the defaults of the WebTransport() constructor.
typedef struct WebTransportOptions {
  // Certificates to accept instead of using the Web PKI. Can be NULL.
  const WebTransportHash *serverCertificateHashes;
  size_t serverCertificateHashesCount;
} WebTransportOptions;

typedef struct WebTransportCloseInfo {
  uint32_t closeCode;
  // Can be NULL for an empty reason.
  const char *reason;
} WebTransportCloseInfo;

// Returns true if the browser supports WebTransport.
bool emscripten_webtransport_is_supported(void);

// Creates a new WebTransport and starts connecting it to the given url, which
// must be an https url with an explicit port. options can be NULL to use the
// defaults.
// Returns a handle > 0 on success. On failure a negative EMSCRIPTEN_RESULT is
// returned: EMSCRIPTEN_RESULT_NOT_SUPPORTED if WebTransport or one of the given
// options is not supported, EMSCRIPTEN_RESULT_INVALID_PARAM if the url or the
// options are invalid.
EMSCRIPTEN_WEBTRANSPORT_T emscripten_webtransport_new(const char *url, const WebTransportOptions *options);

// WebTransport.ready: called once the connection is established (error is
// NULL), or with the error the connection failed with.
typedef void (*em_webtransport_ready_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportError *error, void *userData);
// WebTransport.closed: called once the transport is closed. On a graceful
// close closeInfo holds the code and reason and error is NULL, after an abrupt
// close (e.g. a network failure or a failed connection attempt) closeInfo is
// NULL and error says why.
typedef void (*em_webtransport_closed_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportCloseInfo *closeInfo, const WebTransportError *error, void *userData);

// Sets the callback that is called once the corresponding promise of the
// transport settles. Like awaiting a promise, a callback that is set after the
// promise settled is still called. Setting a callback replaces the previous
// one, pass NULL to remove it.
EMSCRIPTEN_RESULT emscripten_webtransport_set_onready_callback(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, em_webtransport_ready_callback_func callback);
EMSCRIPTEN_RESULT emscripten_webtransport_set_onclosed_callback(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, em_webtransport_closed_callback_func callback);

// WebTransport.close(): closes the session. closeInfo can be NULL to close
// with code 0 and an empty reason.
EMSCRIPTEN_RESULT emscripten_webtransport_close(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportCloseInfo *closeInfo);

// Releases the transport handle, after which it is no longer valid and none of
// its callbacks are called anymore. This does not close the transport.
EMSCRIPTEN_RESULT emscripten_webtransport_delete(EMSCRIPTEN_WEBTRANSPORT_T transport);

// Closes and deletes all transports. You can call this at application exit to
// tear down all connections.
void emscripten_webtransport_deinitialize(void);

#ifdef __cplusplus
} // ~extern "C"
#endif
