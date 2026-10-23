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

// Handle to a stream of a transport. Valid handles are > 0. A stream handle
// refers to either the readable side (a WebTransportReceiveStream) or the
// writable side (a WebTransportSendStream) of a WebTransport stream.
typedef int EMSCRIPTEN_WEBTRANSPORT_STREAM_T;

// Values of the WebTransportError.source field.
typedef enum WebTransportErrorSource {
  // The error is not a WebTransportError (e.g. an InvalidStateError
  // DOMException or a TypeError), so it has no source.
  WEBTRANSPORT_ERROR_SOURCE_NONE = 0,
  WEBTRANSPORT_ERROR_SOURCE_STREAM = 1,
  WEBTRANSPORT_ERROR_SOURCE_SESSION = 2,
} WebTransportErrorSource;

// Describes why an asynchronous operation failed. The browser rejects promises
// with a WebTransportError, a DOMException or another JS error.
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

// Values of the WebTransport.congestionControl field.
typedef enum WebTransportCongestionControl {
  WEBTRANSPORT_CONGESTION_CONTROL_DEFAULT = 0,
  WEBTRANSPORT_CONGESTION_CONTROL_THROUGHPUT = 1,
  WEBTRANSPORT_CONGESTION_CONTROL_LOW_LATENCY = 2,
} WebTransportCongestionControl;

// Values of the WebTransport.reliability field.
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
  // one the server selects is returned by
  // emscripten_webtransport_get_protocol().
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

// Options for creating outgoing streams.
typedef struct WebTransportSendStreamOptions {
  // Queued bytes of streams with a higher send order are sent first.
  int64_t sendOrder;
} WebTransportSendStreamOptions;

// Returns true if the browser supports WebTransport.
bool emscripten_webtransport_is_supported(void);

// Returns the static WebTransport.supportsReliableOnly field, which is true if
// the browser supports sessions over reliable-only transports such as HTTP/2.
bool emscripten_webtransport_supports_reliable_only(void);

// Creates a new WebTransport and starts connecting it to the given url, which
// must be an https url with an explicit port. options can be NULL to use the
// defaults.
// Returns a handle > 0 on success. On failure a negative EMSCRIPTEN_RESULT is
// returned: EMSCRIPTEN_RESULT_NOT_SUPPORTED if WebTransport or one of the given
// options is not supported, EMSCRIPTEN_RESULT_INVALID_PARAM if the url or the
// options are invalid.
EMSCRIPTEN_WEBTRANSPORT_T emscripten_webtransport_new(const char *url, const WebTransportOptions *options);

// Called when the WebTransport.ready promise settles. error is NULL once the
// connection is established, otherwise it holds the reason the connection
// failed.
typedef void (*em_webtransport_ready_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportError *error, void *userData);
// Called when the WebTransport.draining promise fulfills, which happens when
// the server asks to wind down the session before closing it.
typedef void (*em_webtransport_draining_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData);
// Called when the WebTransport.closed promise settles. After a graceful close
// closeInfo holds the close code and reason and error is NULL. After an abrupt
// close, e.g. due to a network failure or a failed connection attempt,
// closeInfo is NULL and error holds the reason.
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

// Returns the WebTransport.reliability field into reliability.
EMSCRIPTEN_RESULT emscripten_webtransport_get_reliability(EMSCRIPTEN_WEBTRANSPORT_T transport, WebTransportReliabilityMode * _Nonnull reliability);

// Returns the WebTransport.congestionControl field into congestionControl. This
// is the congestion control preference the browser applies, which can differ
// from the requested one.
EMSCRIPTEN_RESULT emscripten_webtransport_get_congestion_control(EMSCRIPTEN_WEBTRANSPORT_T transport, WebTransportCongestionControl * _Nonnull congestionControl);

// Writes the WebTransport.protocol field, the protocol selected by the server,
// as a UTF-8 string to the memory area pointed by protocol. If the string does
// not fit into protocolLength bytes including the null terminator, it is
// truncated. The string is empty if the server selected none, or if the
// connection is not established yet.
EMSCRIPTEN_RESULT emscripten_webtransport_get_protocol(EMSCRIPTEN_WEBTRANSPORT_T transport, char * _Nonnull protocol, int protocolLength);
// Returns the buffer size emscripten_webtransport_get_protocol() needs,
// including the null terminator.
EMSCRIPTEN_RESULT emscripten_webtransport_get_protocol_length(EMSCRIPTEN_WEBTRANSPORT_T transport, int * _Nonnull protocolLength);

// Closes the session with WebTransport.close(). closeInfo can be NULL to close
// with code 0 and an empty reason.
EMSCRIPTEN_RESULT emscripten_webtransport_close(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportCloseInfo *closeInfo);

// Releases the transport handle and the handles of all its streams, after which
// they are no longer valid and none of their callbacks are called anymore. This
// does not close the transport.
EMSCRIPTEN_RESULT emscripten_webtransport_delete(EMSCRIPTEN_WEBTRANSPORT_T transport);

// Streams
//
// Streams are reliable and ordered. Their data is read and written with the
// emscripten_webtransport_stream_*() functions below.

// Called with a new stream, or with stream 0 and the reason if there is none.
// An incoming stream callback receives stream 0 and no error once the
// transport is closed and no more streams arrive.
typedef void (*em_webtransport_stream_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData);

// Opens a unidirectional stream to the server with
// WebTransport.createUnidirectionalStream() and calls callback with its
// writable side. options can be NULL.
EMSCRIPTEN_RESULT emscripten_webtransport_create_unidirectional_stream(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportSendStreamOptions *options, void *userData, em_webtransport_stream_callback_func callback);

// Reads the next stream from WebTransport.incomingUnidirectionalStreams, a
// stream opened by the server, and calls callback with its readable side.
EMSCRIPTEN_RESULT emscripten_webtransport_read_incoming_unidirectional_stream(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, em_webtransport_stream_callback_func callback);

// Called with the readable and the writable side of a new bidirectional
// stream, or with both 0 like em_webtransport_stream_callback_func.
typedef void (*em_webtransport_bidirectional_stream_callback_func)(EMSCRIPTEN_WEBTRANSPORT_T transport, EMSCRIPTEN_WEBTRANSPORT_STREAM_T readable, EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable, const WebTransportError *error, void *userData);

// Opens a bidirectional stream to the server with
// WebTransport.createBidirectionalStream(). options can be NULL.
EMSCRIPTEN_RESULT emscripten_webtransport_create_bidirectional_stream(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportSendStreamOptions *options, void *userData, em_webtransport_bidirectional_stream_callback_func callback);

// Reads the next stream from WebTransport.incomingBidirectionalStreams, a
// bidirectional stream opened by the server.
EMSCRIPTEN_RESULT emscripten_webtransport_read_incoming_bidirectional_stream(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData, em_webtransport_bidirectional_stream_callback_func callback);

// Called once a read finished. On success numBytes bytes were written to the
// buffer passed to emscripten_webtransport_stream_read(). done is true once
// the end of the stream is reached, in which case numBytes is 0.
typedef void (*em_webtransport_read_callback_func)(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData);
// Called once a write or close finished. error is NULL on success.
typedef void (*em_webtransport_stream_result_callback_func)(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData);

// Reads up to bufferLength bytes from a readable stream into buffer, which has
// to stay valid until the callback is called. Like recv(), a read returns
// whatever data is available, which can be less than bufferLength, so call it
// in a loop until done is true. Returns EMSCRIPTEN_RESULT_NOT_SUPPORTED for a
// writable stream.
EMSCRIPTEN_RESULT emscripten_webtransport_stream_read(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, void * _Nonnull buffer, size_t bufferLength, void *userData, em_webtransport_read_callback_func callback);

// Writes length bytes to a writable stream. The data is copied, so the buffer
// can be reused right away. callback can be NULL, otherwise it is called once
// the data was handed over to the network. Returns
// EMSCRIPTEN_RESULT_NOT_SUPPORTED for a readable stream.
EMSCRIPTEN_RESULT emscripten_webtransport_stream_write(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const void *data, size_t length, void *userData, em_webtransport_stream_result_callback_func callback);

// Gracefully closes a writable stream after all written data is sent.
// callback can be NULL.
EMSCRIPTEN_RESULT emscripten_webtransport_stream_close(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, void *userData, em_webtransport_stream_result_callback_func callback);

// Aborts a writable stream (WritableStream.abort()), discarding data that was
// not sent yet. The server receives the given application error code.
EMSCRIPTEN_RESULT emscripten_webtransport_stream_abort(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, uint8_t streamErrorCode);

// Cancels a readable stream (ReadableStream.cancel()), asking the server to
// stop sending with the given application error code. Pending and later reads
// finish with done set.
EMSCRIPTEN_RESULT emscripten_webtransport_stream_cancel(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, uint8_t streamErrorCode);

// Releases the stream handle. Pending callbacks of the stream are not called
// anymore. This does not close the stream.
EMSCRIPTEN_RESULT emscripten_webtransport_stream_delete(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream);

// Closes and deletes all transports. You can call this at application exit to
// tear down all connections.
void emscripten_webtransport_deinitialize(void);

#ifdef __cplusplus
} // ~extern "C"
#endif
