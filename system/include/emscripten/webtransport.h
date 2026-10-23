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

// WebTransport.congestionControl
typedef enum WebTransportCongestionControl {
  WEBTRANSPORT_CONGESTION_CONTROL_DEFAULT = 0,
  WEBTRANSPORT_CONGESTION_CONTROL_THROUGHPUT = 1,
  WEBTRANSPORT_CONGESTION_CONTROL_LOW_LATENCY = 2,
} WebTransportCongestionControl;

// WebTransport.reliability
typedef enum WebTransportReliabilityMode {
  // The connection is not established yet.
  WEBTRANSPORT_RELIABILITY_PENDING = 0,
  WEBTRANSPORT_RELIABILITY_RELIABLE_ONLY = 1,
  WEBTRANSPORT_RELIABILITY_SUPPORTS_UNRELIABLE = 2,
} WebTransportReliabilityMode;

// Options for emscripten_webtransport_new(). Zero-initialize the struct to
// get the defaults of the WebTransport() constructor.
typedef struct WebTransportOptions {
  // Allows sharing the network connection with other HTTP/3 sessions.
  bool allowPooling;
  // Fails instead of falling back to HTTP/2 if HTTP/3 is not possible.
  bool requireUnreliable;
  // A hint whether to tune congestion control for throughput or latency.
  WebTransportCongestionControl congestionControl;
  // Application protocols to offer to the server, in order of preference. The
  // one the server selects is returned by emscripten_webtransport_get_protocol().
  const char *const *protocols;
  size_t protocolsCount;
  // Certificates to accept instead of using the Web PKI. Can be NULL. Not
  // supported together with allowPooling.
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

// WebTransport.supportsReliableOnly: returns true if the browser supports
// sessions over reliable-only transports such as HTTP/2.
bool emscripten_webtransport_supports_reliable_only(void);

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
// WebTransport.draining: called once the server asks to wind down the session
// before closing it.
typedef void (*em_webtransport_draining_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData);
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
// Returns EMSCRIPTEN_RESULT_NOT_SUPPORTED if the browser has no
// WebTransport.draining.
EMSCRIPTEN_RESULT emscripten_webtransport_set_ondraining_callback(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, em_webtransport_draining_callback_func callback);

// WebTransport.reliability
EMSCRIPTEN_RESULT emscripten_webtransport_get_reliability(EMSCRIPTEN_WEBTRANSPORT_T transport, WebTransportReliabilityMode * _Nonnull reliability);

// WebTransport.congestionControl: the congestion control preference the
// browser applies, which can differ from the requested one.
EMSCRIPTEN_RESULT emscripten_webtransport_get_congestion_control(EMSCRIPTEN_WEBTRANSPORT_T transport, WebTransportCongestionControl * _Nonnull congestionControl);

// WebTransport.protocol: writes the protocol selected by the server as a UTF-8
// string to protocol, truncated to protocolLength bytes including the null
// terminator. The string is empty if the server selected none, or if the
// connection is not established yet.
EMSCRIPTEN_RESULT emscripten_webtransport_get_protocol(EMSCRIPTEN_WEBTRANSPORT_T transport, char * _Nonnull protocol, int protocolLength);
// Returns the buffer size emscripten_webtransport_get_protocol() needs,
// including the null terminator.
EMSCRIPTEN_RESULT emscripten_webtransport_get_protocol_length(EMSCRIPTEN_WEBTRANSPORT_T transport, int * _Nonnull protocolLength);

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
