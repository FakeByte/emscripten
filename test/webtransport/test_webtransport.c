// Tests the WebTransport bindings against a public WebTransport echo server.
//
// The tests run one after another. Each test starts some asynchronous
// operations and calls next_test() from a callback once it is done.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten/emscripten.h>
#include <emscripten/webtransport.h>

#define ECHO_SERVER_URL "https://wt-ord.akaleapi.net:6161/echo"
// The server rejects sessions for unknown paths.
#define REJECTING_URL "https://wt-ord.akaleapi.net:6161/does-not-exist"

// Time a single test may take before the test run fails.
#define TEST_TIMEOUT_MS 30000

static const char *current_test_name = "";

#define CHECK(cond) do { \
    if (!(cond)) { \
      printf("FAIL [%s] %s:%d: %s\n", current_test_name, __FILE__, __LINE__, #cond); \
      emscripten_force_exit(1); \
    } \
  } while (0)

#define CHECK_RESULT(expr, expected) do { \
    EMSCRIPTEN_RESULT result_ = (expr); \
    if (result_ != (expected)) { \
      printf("FAIL [%s] %s:%d: %s returned %d, expected %d\n", current_test_name, __FILE__, __LINE__, #expr, result_, (expected)); \
      emscripten_force_exit(1); \
    } \
  } while (0)

#define CHECK_SUCCESS(expr) CHECK_RESULT(expr, EMSCRIPTEN_RESULT_SUCCESS)

#define CHECK_NO_ERROR(error) do { \
    const WebTransportError *error_ = (error); \
    if (error_) { \
      printf("FAIL [%s] %s:%d: unexpected %s: %s\n", current_test_name, __FILE__, __LINE__, error_->name, error_->message); \
      emscripten_force_exit(1); \
    } \
  } while (0)

static void print_error(const char *what, const WebTransportError *error) {
  printf("  %s: %s (%s), source=%d, streamErrorCode=%d\n", what, error->name, error->message, error->source, error->streamErrorCode);
}

// Test runner.

typedef struct Test {
  const char *name;
  void (*run)(void);
} Test;

static const Test *tests;
static int num_tests;
static int current_test = -1;
static int watchdog = 0;

static void on_timeout(void *userData) {
  printf("FAIL [%s] timed out after %d ms\n", current_test_name, TEST_TIMEOUT_MS);
  emscripten_force_exit(1);
}

static void next_test(void) {
  if (watchdog) emscripten_clear_timeout(watchdog);
  if (current_test >= 0) printf("  PASS\n\n");
  if (++current_test == num_tests) {
    printf("All %d tests passed\n", num_tests);
    emscripten_force_exit(0);
  }
  current_test_name = tests[current_test].name;
  printf("Test %d/%d: %s\n", current_test + 1, num_tests, current_test_name);
  watchdog = emscripten_set_timeout(on_timeout, TEST_TIMEOUT_MS, NULL);
  tests[current_test].run();
}

// Callbacks that must not be called.

static void fail_on_ready(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportError *error, void *userData) {
  printf("FAIL [%s] unexpected ready callback for transport %d\n", current_test_name, transport);
  emscripten_force_exit(1);
}

static void fail_on_closed(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportCloseInfo *closeInfo, const WebTransportError *error, void *userData) {
  printf("FAIL [%s] unexpected closed callback for transport %d\n", current_test_name, transport);
  emscripten_force_exit(1);
}

// When connecting fails, the ready and closed callbacks both receive an error.

static int failed_callbacks;

// Called once both callbacks ran, in whichever order the browser settles the
// promises.
static void finish_connection_failure(EMSCRIPTEN_WEBTRANSPORT_T transport) {
  CHECK_SUCCESS(emscripten_webtransport_delete(transport));
  next_test();
}

static void on_failed_ready(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportError *error, void *userData) {
  CHECK(userData == (void *)0x1234);
  CHECK(error);
  print_error("ready failed", error);
  CHECK(strcmp(error->name, "WebTransportError") == 0);
  CHECK(error->source == WEBTRANSPORT_ERROR_SOURCE_SESSION);
  CHECK(error->streamErrorCode == -1);
  if (++failed_callbacks == 2) finish_connection_failure(transport);
}

static void on_failed_closed(EMSCRIPTEN_WEBTRANSPORT_T transport, const WebTransportCloseInfo *closeInfo, const WebTransportError *error, void *userData) {
  CHECK(userData == (void *)0x5678);
  CHECK(!closeInfo);
  CHECK(error);
  print_error("closed with", error);
  CHECK(strcmp(error->name, "WebTransportError") == 0);
  if (++failed_callbacks == 2) finish_connection_failure(transport);
}

static void expect_connection_failure(EMSCRIPTEN_WEBTRANSPORT_T transport) {
  CHECK(transport > 0);
  failed_callbacks = 0;
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(transport, (void *)0x1234, on_failed_ready));
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(transport, (void *)0x5678, on_failed_closed));
}

// Tests.

static void test_is_supported(void) {
  CHECK(emscripten_webtransport_is_supported());
  next_test();
}

static void test_invalid_arguments(void) {
  CHECK_RESULT(emscripten_webtransport_new(NULL, NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  // Not https.
  CHECK_RESULT(emscripten_webtransport_new("http://wt-ord.akaleapi.net:6161/echo", NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_new("not a url", NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  // Fragments are not allowed.
  CHECK_RESULT(emscripten_webtransport_new(ECHO_SERVER_URL "#fragment", NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);

  // Handles that were never allocated.
  CHECK_RESULT(emscripten_webtransport_set_onready_callback(0, NULL, fail_on_ready), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_set_onclosed_callback(12345, NULL, fail_on_closed), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_close(-1, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_delete(12345), EMSCRIPTEN_RESULT_INVALID_TARGET);
  next_test();
}

static void test_rejected_connection(void) {
  expect_connection_failure(emscripten_webtransport_new(REJECTING_URL, NULL));
}

static void test_certificate_hash_mismatch(void) {
  // With serverCertificateHashes the browser only accepts a certificate with
  // one of the given hashes, so connecting fails with a hash that cannot match.
  static const uint8_t zeros[32];
  WebTransportHash hash = {.algorithm = "sha-256", .value = zeros, .valueLength = sizeof(zeros)};
  WebTransportOptions options = {.serverCertificateHashes = &hash, .serverCertificateHashesCount = 1};
  expect_connection_failure(emscripten_webtransport_new(ECHO_SERVER_URL, &options));
}

static void test_close_while_connecting(void) {
  EMSCRIPTEN_WEBTRANSPORT_T transport = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  expect_connection_failure(transport);
  CHECK_SUCCESS(emscripten_webtransport_close(transport, NULL));
}

// Connects, registers callbacks late and replaces callbacks, then closes with a
// code and reason that the closed callback has to receive.

static EMSCRIPTEN_WEBTRANSPORT_T transport;
static int ready_calls;

static void on_closed_with_info(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportCloseInfo *closeInfo, const WebTransportError *error, void *userData) {
  CHECK(t == transport);
  CHECK(userData == (void *)2);
  CHECK_NO_ERROR(error);
  CHECK(closeInfo);
  printf("  closed: closeCode=%u, reason=%s\n", closeInfo->closeCode, closeInfo->reason);
  CHECK(closeInfo->closeCode == 4000000000u);
  CHECK(strcmp(closeInfo->reason, "Test complete \xe2\x9c\x93") == 0);
  CHECK(ready_calls == 2);

  CHECK_SUCCESS(emscripten_webtransport_delete(transport));
  // The handle is gone.
  CHECK_RESULT(emscripten_webtransport_close(transport, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_delete(transport), EMSCRIPTEN_RESULT_INVALID_TARGET);
  next_test();
}

static void on_late_ready(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportError *error, void *userData) {
  CHECK(t == transport);
  CHECK_NO_ERROR(error);
  CHECK(userData == (void *)0xabcd);
  CHECK(++ready_calls == 2);
  WebTransportCloseInfo closeInfo = {.closeCode = 4000000000u, .reason = "Test complete \xe2\x9c\x93"};
  CHECK_SUCCESS(emscripten_webtransport_close(transport, &closeInfo));
}

static void on_ready(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportError *error, void *userData) {
  CHECK(t == transport);
  CHECK_NO_ERROR(error);
  CHECK(userData == (void *)0x1234);
  CHECK(++ready_calls == 1);
  // The ready promise is already fulfilled, the new callback is still called.
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(transport, (void *)0xabcd, on_late_ready));
}

static void test_connect_and_close(void) {
  // A zeroed options struct means the defaults.
  WebTransportOptions options = {0};
  transport = emscripten_webtransport_new(ECHO_SERVER_URL, &options);
  CHECK(transport > 0);
  ready_calls = 0;
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(transport, (void *)0x1234, on_ready));
  // Replaced callbacks are not called.
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(transport, NULL, fail_on_closed));
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(transport, NULL, NULL));
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(transport, (void *)2, on_closed_with_info));
}

// A deleted transport does not call its callbacks anymore. The test waits for a
// second connection to become ready, by which time the deleted one would have
// called its callbacks.

static EMSCRIPTEN_WEBTRANSPORT_T deleted_transport;

static void on_other_closed(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportCloseInfo *closeInfo, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK_SUCCESS(emscripten_webtransport_delete(t));
  next_test();
}

static void on_other_ready(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(t, NULL, on_other_closed));
  CHECK_SUCCESS(emscripten_webtransport_close(t, NULL));
}

static void test_delete_removes_callbacks(void) {
  deleted_transport = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  CHECK(deleted_transport > 0);
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(deleted_transport, NULL, fail_on_ready));
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(deleted_transport, NULL, fail_on_closed));
  CHECK_SUCCESS(emscripten_webtransport_close(deleted_transport, NULL));
  CHECK_SUCCESS(emscripten_webtransport_delete(deleted_transport));

  EMSCRIPTEN_WEBTRANSPORT_T other = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  CHECK(other > 0);
  // Handles are reused after deletion, the deleted transport's callbacks must
  // not leak into the new one.
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(other, NULL, on_other_ready));
}

static void test_supports_reliable_only(void) {
  bool supported = emscripten_webtransport_supports_reliable_only();
  printf("  supportsReliableOnly: %d\n", supported);
  CHECK(supported == EM_ASM_INT(return !!WebTransport.supportsReliableOnly));
  next_test();
}

static void test_invalid_options(void) {
  WebTransportOptions options = {.congestionControl = (WebTransportCongestionControl)7};
  CHECK_RESULT(emscripten_webtransport_new(ECHO_SERVER_URL, &options), EMSCRIPTEN_RESULT_INVALID_PARAM);

  const char *duplicate_protocols[] = {"echo", "echo"};
  options = (WebTransportOptions){.protocols = duplicate_protocols, .protocolsCount = 2};
  CHECK_RESULT(emscripten_webtransport_new(ECHO_SERVER_URL, &options), EMSCRIPTEN_RESULT_INVALID_PARAM);

  const char *empty_protocol[] = {""};
  options = (WebTransportOptions){.protocols = empty_protocol, .protocolsCount = 1};
  CHECK_RESULT(emscripten_webtransport_new(ECHO_SERVER_URL, &options), EMSCRIPTEN_RESULT_INVALID_PARAM);

  // Certificate hashes require a dedicated connection.
  static const uint8_t zeros[32];
  WebTransportHash hash = {.algorithm = "sha-256", .value = zeros, .valueLength = sizeof(zeros)};
  options = (WebTransportOptions){.allowPooling = true, .serverCertificateHashes = &hash, .serverCertificateHashesCount = 1};
  CHECK_RESULT(emscripten_webtransport_new(ECHO_SERVER_URL, &options), EMSCRIPTEN_RESULT_NOT_SUPPORTED);

  WebTransportReliabilityMode reliability;
  WebTransportCongestionControl congestionControl;
  int length;
  char protocol[16];
  CHECK_RESULT(emscripten_webtransport_set_ondraining_callback(12345, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_get_reliability(12345, &reliability), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_get_congestion_control(12345, &congestionControl), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_get_protocol(12345, protocol, sizeof(protocol)), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_get_protocol_length(12345, &length), EMSCRIPTEN_RESULT_INVALID_TARGET);
  next_test();
}

// Connects with all options set and checks the properties of the connection.

static void fail_on_draining(EMSCRIPTEN_WEBTRANSPORT_T transport, void *userData) {
  printf("FAIL [%s] unexpected draining callback for transport %d\n", current_test_name, transport);
  emscripten_force_exit(1);
}

static void on_properties_closed(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportCloseInfo *closeInfo, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK_SUCCESS(emscripten_webtransport_delete(t));
  next_test();
}

static void on_properties_ready(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);

  WebTransportReliabilityMode reliability;
  CHECK_SUCCESS(emscripten_webtransport_get_reliability(t, &reliability));
  printf("  reliability: %d\n", reliability);
  // The echo server is reached over HTTP/3.
  CHECK(reliability == WEBTRANSPORT_RELIABILITY_SUPPORTS_UNRELIABLE);

  // The browser may not honor the requested low-latency preference.
  WebTransportCongestionControl congestionControl;
  CHECK_SUCCESS(emscripten_webtransport_get_congestion_control(t, &congestionControl));
  printf("  congestionControl: %d\n", congestionControl);
  CHECK(congestionControl == WEBTRANSPORT_CONGESTION_CONTROL_DEFAULT || congestionControl == WEBTRANSPORT_CONGESTION_CONTROL_LOW_LATENCY);

  // The echo server does not select any of the offered protocols.
  int length = 0;
  CHECK_SUCCESS(emscripten_webtransport_get_protocol_length(t, &length));
  CHECK(length == 1);
  char protocol[16] = "garbage";
  CHECK_SUCCESS(emscripten_webtransport_get_protocol(t, protocol, sizeof(protocol)));
  CHECK(strcmp(protocol, "") == 0);
  CHECK_RESULT(emscripten_webtransport_get_protocol(t, NULL, 0), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_get_protocol_length(t, NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);

  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(t, NULL, on_properties_closed));
  CHECK_SUCCESS(emscripten_webtransport_close(t, NULL));
}

static void test_connection_properties(void) {
  const char *protocols[] = {"echo-v2", "echo-v1"};
  WebTransportOptions options = {
    .allowPooling = false,
    .requireUnreliable = true,
    .congestionControl = WEBTRANSPORT_CONGESTION_CONTROL_LOW_LATENCY,
    .protocols = protocols,
    .protocolsCount = 2,
  };
  EMSCRIPTEN_WEBTRANSPORT_T t = emscripten_webtransport_new(ECHO_SERVER_URL, &options);
  CHECK(t > 0);

  WebTransportReliabilityMode reliability;
  CHECK_SUCCESS(emscripten_webtransport_get_reliability(t, &reliability));
  CHECK(reliability == WEBTRANSPORT_RELIABILITY_PENDING);
  int length = 0;
  CHECK_SUCCESS(emscripten_webtransport_get_protocol_length(t, &length));
  CHECK(length == 1);

  // The echo server never asks to drain the session.
  CHECK_SUCCESS(emscripten_webtransport_set_ondraining_callback(t, NULL, fail_on_draining));
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(t, NULL, on_properties_ready));
}

// Helpers for the stream tests.

// Reads a stream to its end in reads of chunkSize bytes, then calls on_done.
typedef struct StreamReader {
  EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream;
  size_t chunkSize;
  void (*on_done)(struct StreamReader *reader);
  uint8_t *data;
  size_t length;
  size_t capacity;
} StreamReader;

static void read_next_chunk(StreamReader *reader);

static void on_chunk_read(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData) {
  StreamReader *reader = userData;
  CHECK(stream == reader->stream);
  CHECK_NO_ERROR(error);
  if (done) {
    CHECK(numBytes == 0);
    reader->on_done(reader);
    return;
  }
  CHECK(numBytes > 0 && numBytes <= reader->chunkSize);
  reader->length += numBytes;
  read_next_chunk(reader);
}

static void read_next_chunk(StreamReader *reader) {
  if (reader->capacity - reader->length < reader->chunkSize) {
    reader->capacity = reader->length + reader->chunkSize;
    reader->data = realloc(reader->data, reader->capacity);
  }
  CHECK_SUCCESS(emscripten_webtransport_stream_read(reader->stream, reader->data + reader->length, reader->chunkSize, reader, on_chunk_read));
}

static void start_reading(StreamReader *reader, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t chunkSize, void (*on_done)(StreamReader *reader)) {
  free(reader->data);
  *reader = (StreamReader){.stream = stream, .chunkSize = chunkSize, .on_done = on_done};
  read_next_chunk(reader);
}

static void fill_pattern(uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) data[i] = (uint8_t)(i * 31 + i / 251);
}

static void check_write_succeeded(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
}

static void fail_on_read(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData) {
  printf("FAIL [%s] unexpected read callback for stream %d\n", current_test_name, stream);
  emscripten_force_exit(1);
}

static void fail_on_stream(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  printf("FAIL [%s] unexpected stream callback for transport %d\n", current_test_name, t);
  emscripten_force_exit(1);
}

static void fail_on_bidi_stream(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T readable, EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable, const WebTransportError *error, void *userData) {
  printf("FAIL [%s] unexpected bidirectional stream callback for transport %d\n", current_test_name, t);
  emscripten_force_exit(1);
}

// Checks that reading functions fail on a writable stream.
static void check_not_readable(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream) {
  char buffer[4];
  CHECK_RESULT(emscripten_webtransport_stream_read(stream, buffer, sizeof(buffer), NULL, fail_on_read), EMSCRIPTEN_RESULT_NOT_SUPPORTED);
  CHECK_RESULT(emscripten_webtransport_stream_cancel(stream, 0), EMSCRIPTEN_RESULT_NOT_SUPPORTED);
}

// Checks that writing functions fail on a readable stream.
static void check_not_writable(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream) {
  CHECK_RESULT(emscripten_webtransport_stream_write(stream, "x", 1, NULL, NULL), EMSCRIPTEN_RESULT_NOT_SUPPORTED);
  CHECK_RESULT(emscripten_webtransport_stream_close(stream, NULL, NULL), EMSCRIPTEN_RESULT_NOT_SUPPORTED);
  CHECK_RESULT(emscripten_webtransport_stream_abort(stream, 0), EMSCRIPTEN_RESULT_NOT_SUPPORTED);
}

// The stream tests share one connection, which is opened by
// test_open_shared_transport() and closed by test_close_shared_transport().
static EMSCRIPTEN_WEBTRANSPORT_T shared;
static StreamReader reader;
static int pending_steps;

static void on_shared_ready(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  next_test();
}

static void test_open_shared_transport(void) {
  shared = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  CHECK(shared > 0);
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(shared, NULL, on_shared_ready));
}

static void test_stream_invalid_arguments(void) {
  char buffer[4];
  CHECK_RESULT(emscripten_webtransport_create_unidirectional_stream(12345, NULL, NULL, fail_on_stream), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_create_unidirectional_stream(shared, NULL, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_read_incoming_unidirectional_stream(12345, NULL, fail_on_stream), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_read_incoming_unidirectional_stream(shared, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_create_bidirectional_stream(12345, NULL, NULL, fail_on_bidi_stream), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_create_bidirectional_stream(shared, NULL, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_read_incoming_bidirectional_stream(12345, NULL, fail_on_bidi_stream), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_read_incoming_bidirectional_stream(shared, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_stream_read(12345, buffer, sizeof(buffer), NULL, fail_on_read), EMSCRIPTEN_RESULT_INVALID_TARGET);  CHECK_RESULT(emscripten_webtransport_stream_write(12345, "x", 1, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_stream_close(12345, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_stream_abort(12345, 0), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_stream_cancel(12345, 0), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_stream_delete(12345), EMSCRIPTEN_RESULT_INVALID_TARGET);
  next_test();
}

// Sends a message on a unidirectional stream in two writes. The server echoes
// it on a unidirectional stream of its own, which is read in small chunks.

static const char uni_message[] = "Hello, unidirectional world!";
static EMSCRIPTEN_WEBTRANSPORT_STREAM_T uni_writable;

static void finish_unidirectional_test(void) {
  if (--pending_steps) return;
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(reader.stream));
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(uni_writable));
  CHECK_RESULT(emscripten_webtransport_stream_delete(uni_writable), EMSCRIPTEN_RESULT_INVALID_TARGET);
  next_test();
}

static void on_uni_echo_read(StreamReader *r) {
  printf("  received %zu bytes: %.*s\n", r->length, (int)r->length, r->data);
  CHECK(r->length == strlen(uni_message));
  CHECK(memcmp(r->data, uni_message, r->length) == 0);
  finish_unidirectional_test();
}

static void on_incoming_uni_stream(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK(t == shared);
  CHECK(userData == (void *)0x77);
  CHECK_NO_ERROR(error);
  CHECK(stream > 0);
  check_not_writable(stream);
  char buffer[4];
  CHECK_RESULT(emscripten_webtransport_stream_read(stream, NULL, 4, NULL, fail_on_read), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_stream_read(stream, buffer, 0, NULL, fail_on_read), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_stream_read(stream, buffer, sizeof(buffer), NULL, NULL), EMSCRIPTEN_RESULT_INVALID_PARAM);
  start_reading(&reader, stream, 5, on_uni_echo_read);
}

static void on_uni_closed(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK(stream == uni_writable);
  CHECK(userData == (void *)0x99);
  CHECK_NO_ERROR(error);
  finish_unidirectional_test();
}

static void on_second_write(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK(stream == uni_writable);
  CHECK(userData == (void *)0x88);
  CHECK_NO_ERROR(error);
  finish_unidirectional_test();
}

static void on_uni_created(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK(t == shared);
  CHECK(userData == (void *)0x66);
  CHECK_NO_ERROR(error);
  CHECK(stream > 0);
  uni_writable = stream;
  check_not_readable(stream);
  // The data is copied, so the buffer can change right after the write.
  char message[sizeof(uni_message)];
  strcpy(message, uni_message);
  CHECK_SUCCESS(emscripten_webtransport_stream_write(stream, message, 7, NULL, NULL));
  memset(message, '?', 7);
  CHECK_SUCCESS(emscripten_webtransport_stream_write(stream, message + 7, strlen(message) - 7, (void *)0x88, on_second_write));
  CHECK_SUCCESS(emscripten_webtransport_stream_close(stream, (void *)0x99, on_uni_closed));
}

static void test_unidirectional_stream(void) {
  // Wait for the incoming stream, the second write and the close.
  pending_steps = 3;
  // Waiting for an incoming stream can start before there is one.
  CHECK_SUCCESS(emscripten_webtransport_read_incoming_unidirectional_stream(shared, (void *)0x77, on_incoming_uni_stream));
  WebTransportSendOptions options = {.sendOrder = 5};
  CHECK_SUCCESS(emscripten_webtransport_create_unidirectional_stream(shared, &options, (void *)0x66, on_uni_created));
}

// Echoes a large message, which arrives in many reads.

#define LARGE_MESSAGE_SIZE 200000
static uint8_t *large_message;

static void on_large_echo_read(StreamReader *r) {
  printf("  received %zu bytes\n", r->length);
  CHECK(r->length == LARGE_MESSAGE_SIZE);
  CHECK(memcmp(r->data, large_message, LARGE_MESSAGE_SIZE) == 0);
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(r->stream));
  free(large_message);
  next_test();
}

static void on_large_incoming(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK(stream > 0);
  start_reading(&reader, stream, 65536, on_large_echo_read);
}

static void on_large_created(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK_SUCCESS(emscripten_webtransport_stream_write(stream, large_message, LARGE_MESSAGE_SIZE, NULL, check_write_succeeded));
  CHECK_SUCCESS(emscripten_webtransport_stream_close(stream, NULL, check_write_succeeded));
  // Deleting the handle does not close the stream or discard written data.
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(stream));
  CHECK_SUCCESS(emscripten_webtransport_read_incoming_unidirectional_stream(shared, NULL, on_large_incoming));
}

static void test_large_unidirectional_stream(void) {
  large_message = malloc(LARGE_MESSAGE_SIZE);
  fill_pattern(large_message, LARGE_MESSAGE_SIZE);
  CHECK_SUCCESS(emscripten_webtransport_create_unidirectional_stream(shared, NULL, NULL, on_large_created));
}

// Echoes a message on a bidirectional stream. Before that, a read on another
// bidirectional stream is left pending and its handle is deleted. By the time
// the second echo arrives the first one has arrived too, which must not call
// the deleted stream's read callback.

static const char bidi_message[] = "Hello, bidirectional world!";
static EMSCRIPTEN_WEBTRANSPORT_STREAM_T bidi_writable;

static void finish_bidirectional_test(void) {
  if (--pending_steps) return;
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(reader.stream));
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(bidi_writable));
  next_test();
}

static void on_bidi_echo_read(StreamReader *r) {
  printf("  received %zu bytes: %.*s\n", r->length, (int)r->length, r->data);
  CHECK(r->length == strlen(bidi_message));
  CHECK(memcmp(r->data, bidi_message, r->length) == 0);
  finish_bidirectional_test();
}

static void on_bidi_closed(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  finish_bidirectional_test();
}

static void on_bidi_created(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T readable, EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable, const WebTransportError *error, void *userData) {
  CHECK(t == shared);
  CHECK(userData == (void *)0x55);
  CHECK_NO_ERROR(error);
  CHECK(readable > 0 && writable > 0 && readable != writable);
  check_not_writable(readable);
  check_not_readable(writable);
  bidi_writable = writable;
  start_reading(&reader, readable, 8, on_bidi_echo_read);
  CHECK_SUCCESS(emscripten_webtransport_stream_write(writable, bidi_message, strlen(bidi_message), NULL, check_write_succeeded));
  CHECK_SUCCESS(emscripten_webtransport_stream_close(writable, NULL, on_bidi_closed));
}

static void on_abandoned_bidi_created(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T readable, EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  static char buffer[64];
  CHECK_SUCCESS(emscripten_webtransport_stream_read(readable, buffer, sizeof(buffer), NULL, fail_on_read));
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(readable));
  CHECK_SUCCESS(emscripten_webtransport_stream_write(writable, "echo", 4, NULL, NULL));
  CHECK_SUCCESS(emscripten_webtransport_stream_close(writable, NULL, NULL));
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(writable));

  WebTransportSendOptions options = {.sendOrder = -3};
  CHECK_SUCCESS(emscripten_webtransport_create_bidirectional_stream(shared, &options, (void *)0x55, on_bidi_created));
}

static void test_bidirectional_stream(void) {
  // Wait for the end of the echo and for the close.
  pending_steps = 2;
  CHECK_SUCCESS(emscripten_webtransport_create_bidirectional_stream(shared, NULL, NULL, on_abandoned_bidi_created));
}

// Sends datagrams and reads their echoes. Datagrams can get lost, so the same
// datagram is sent repeatedly until echoes arrive.

#define DATAGRAM_SIZE 100
static uint8_t datagram[DATAGRAM_SIZE];
static EMSCRIPTEN_WEBTRANSPORT_STREAM_T datagrams_readable;
static EMSCRIPTEN_WEBTRANSPORT_STREAM_T datagrams_writable;
static int resend_interval;
static uint8_t datagram_buffer[1024];

static void send_datagram(void *userData) {
  CHECK_SUCCESS(emscripten_webtransport_stream_write(datagrams_writable, datagram, sizeof(datagram), NULL, check_write_succeeded));
}

static void start_sending_datagrams(EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable) {
  datagrams_writable = writable;
  send_datagram(NULL);
  resend_interval = emscripten_set_interval(send_datagram, 200, NULL);
}

static void finish_datagram_test(void) {
  emscripten_clear_interval(resend_interval);
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(datagrams_writable));
  next_test();
}

static void on_truncated_datagram(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK(!done);
  printf("  received truncated datagram of %zu bytes\n", numBytes);
  CHECK(numBytes == 10);
  CHECK(memcmp(datagram_buffer, datagram, 10) == 0);
  finish_datagram_test();
}

static void on_datagram(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData) {
  CHECK(stream == datagrams_readable);
  CHECK(userData == (void *)0x44);
  CHECK_NO_ERROR(error);
  CHECK(!done);
  printf("  received datagram of %zu bytes\n", numBytes);
  CHECK(numBytes == DATAGRAM_SIZE);
  CHECK(memcmp(datagram_buffer, datagram, DATAGRAM_SIZE) == 0);
  // A datagram that does not fit into the buffer is truncated.
  CHECK_SUCCESS(emscripten_webtransport_stream_read(datagrams_readable, datagram_buffer, 10, NULL, on_truncated_datagram));
}

static void test_datagrams(void) {
  size_t maxDatagramSize = 0;
  CHECK_SUCCESS(emscripten_webtransport_get_max_datagram_size(shared, &maxDatagramSize));
  printf("  maxDatagramSize: %zu\n", maxDatagramSize);
  CHECK(maxDatagramSize >= DATAGRAM_SIZE);

  double maxAge = 0;
  CHECK_SUCCESS(emscripten_webtransport_get_incoming_max_age(shared, &maxAge));
  printf("  incomingMaxAge: %f\n", maxAge);
  CHECK(maxAge > 0);
  CHECK_SUCCESS(emscripten_webtransport_set_incoming_max_age(shared, 5000));
  CHECK_SUCCESS(emscripten_webtransport_get_incoming_max_age(shared, &maxAge));
  CHECK(maxAge == 5000);
  CHECK_SUCCESS(emscripten_webtransport_get_outgoing_max_age(shared, &maxAge));
  printf("  outgoingMaxAge: %f\n", maxAge);
  CHECK(maxAge > 0);
  CHECK_SUCCESS(emscripten_webtransport_set_outgoing_max_age(shared, 2500.5));
  CHECK_SUCCESS(emscripten_webtransport_get_outgoing_max_age(shared, &maxAge));
  CHECK(maxAge == 2500.5);
  CHECK_RESULT(emscripten_webtransport_set_incoming_max_age(shared, -1), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_RESULT(emscripten_webtransport_set_outgoing_max_age(shared, -1), EMSCRIPTEN_RESULT_INVALID_PARAM);
  CHECK_SUCCESS(emscripten_webtransport_get_outgoing_max_age(shared, &maxAge));
  CHECK(maxAge == 2500.5);

  datagrams_readable = emscripten_webtransport_get_datagrams_readable(shared);
  CHECK(datagrams_readable > 0);
  CHECK(emscripten_webtransport_get_datagrams_readable(shared) == datagrams_readable);
  check_not_writable(datagrams_readable);

  WebTransportSendOptions options = {.sendOrder = 2};
  EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable = emscripten_webtransport_create_datagrams_writable(shared, &options);
  CHECK(writable > 0);
  check_not_readable(writable);

  for (int i = 0; i < DATAGRAM_SIZE; i++) datagram[i] = 'a' + i % 26;
  CHECK_SUCCESS(emscripten_webtransport_stream_read(datagrams_readable, datagram_buffer, sizeof(datagram_buffer), (void *)0x44, on_datagram));
  start_sending_datagrams(writable);
}

// Without WebTransportDatagramDuplexStream.createWritable(), as in Chrome, the
// deprecated datagrams.writable is used instead.

static void read_new_datagram(void);

static void on_new_datagram(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK(!done);
  // Skip the echoes of the previous test that are still arriving.
  if (numBytes != DATAGRAM_SIZE || memcmp(datagram_buffer, datagram, DATAGRAM_SIZE) != 0) {
    read_new_datagram();
    return;
  }
  finish_datagram_test();
}

static void read_new_datagram(void) {
  CHECK_SUCCESS(emscripten_webtransport_stream_read(datagrams_readable, datagram_buffer, sizeof(datagram_buffer), NULL, on_new_datagram));
}

static void test_datagrams_without_create_writable(void) {
  EM_ASM({
    var proto = WebTransportDatagramDuplexStream.prototype;
    Module.savedCreateWritable = proto.createWritable;
    delete proto.createWritable;
  });
  CHECK(EM_ASM_INT(return 'createWritable' in WebTransportDatagramDuplexStream.prototype) == 0);
  EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable = emscripten_webtransport_create_datagrams_writable(shared, NULL);
  EM_ASM(WebTransportDatagramDuplexStream.prototype.createWritable = Module.savedCreateWritable);
  CHECK(writable > 0);

  // A deleted datagrams readable handle is replaced by a new one.
  CHECK_SUCCESS(emscripten_webtransport_stream_delete(datagrams_readable));
  datagrams_readable = emscripten_webtransport_get_datagrams_readable(shared);
  CHECK(datagrams_readable > 0);

  for (int i = 0; i < DATAGRAM_SIZE; i++) datagram[i] = 'z' - i % 26;
  read_new_datagram();
  start_sending_datagrams(writable);
}

// Closing the transport finishes waiting for incoming streams and datagrams.
// The echo server never opens bidirectional streams, so that wait only ends
// here.

static void finish_close_step(void) {
  if (--pending_steps) return;
  CHECK_SUCCESS(emscripten_webtransport_delete(shared));
  next_test();
}

static void on_no_more_bidi_streams(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T readable, EMSCRIPTEN_WEBTRANSPORT_STREAM_T writable, const WebTransportError *error, void *userData) {
  CHECK(t == shared);
  CHECK_NO_ERROR(error);
  CHECK(readable == 0 && writable == 0);
  finish_close_step();
}

static void on_no_more_uni_streams(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK(t == shared);
  CHECK_NO_ERROR(error);
  CHECK(stream == 0);
  finish_close_step();
}

static void on_shared_closed(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportCloseInfo *closeInfo, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  finish_close_step();
}

// Echoes of earlier datagrams can still arrive, so this reads datagrams until
// the reads are done.
static void on_datagram_until_done(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  if (done) {
    CHECK(numBytes == 0);
    finish_close_step();
    return;
  }
  CHECK_SUCCESS(emscripten_webtransport_stream_read(stream, datagram_buffer, sizeof(datagram_buffer), NULL, on_datagram_until_done));
}

static void test_close_shared_transport(void) {
  pending_steps = 4;
  CHECK_SUCCESS(emscripten_webtransport_read_incoming_unidirectional_stream(shared, NULL, on_no_more_uni_streams));
  CHECK_SUCCESS(emscripten_webtransport_read_incoming_bidirectional_stream(shared, NULL, on_no_more_bidi_streams));
  CHECK_SUCCESS(emscripten_webtransport_stream_read(datagrams_readable, datagram_buffer, sizeof(datagram_buffer), NULL, on_datagram_until_done));
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(shared, NULL, on_shared_closed));
  CHECK_SUCCESS(emscripten_webtransport_close(shared, NULL));
}

// Aborting and canceling streams. These run on a connection of their own since
// the server's reaction to them must not interfere with other tests.

static EMSCRIPTEN_WEBTRANSPORT_T reset_transport;
static EMSCRIPTEN_WEBTRANSPORT_STREAM_T aborted_stream;

static void on_cancel_read(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, size_t numBytes, bool done, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK(done);
  CHECK(numBytes == 0);
  CHECK_SUCCESS(emscripten_webtransport_close(reset_transport, NULL));
  // Deleting the transport deletes its streams.
  CHECK_SUCCESS(emscripten_webtransport_delete(reset_transport));
  CHECK_RESULT(emscripten_webtransport_stream_delete(stream), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_stream_write(aborted_stream, "x", 1, NULL, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  next_test();
}

static void on_incoming_to_cancel(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK(stream > 0);
  CHECK_SUCCESS(emscripten_webtransport_stream_cancel(stream, 4));
  // After canceling, reads finish without data.
  static char buffer[16];
  CHECK_SUCCESS(emscripten_webtransport_stream_read(stream, buffer, sizeof(buffer), NULL, on_cancel_read));
}

static void on_stream_to_echo(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK_SUCCESS(emscripten_webtransport_stream_write(stream, "cancel me", 9, NULL, check_write_succeeded));
  CHECK_SUCCESS(emscripten_webtransport_stream_close(stream, NULL, check_write_succeeded));
  CHECK_SUCCESS(emscripten_webtransport_read_incoming_unidirectional_stream(reset_transport, NULL, on_incoming_to_cancel));
}

static void finish_abort(void) {
  if (--pending_steps) return;
  CHECK_SUCCESS(emscripten_webtransport_create_unidirectional_stream(reset_transport, NULL, NULL, on_stream_to_echo));
}

static void on_write_after_abort(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK(error);
  print_error("write after abort", error);
  CHECK(strcmp(error->name, "WebTransportError") == 0);
  CHECK(error->source == WEBTRANSPORT_ERROR_SOURCE_STREAM);
  CHECK(error->streamErrorCode == 7);
  finish_abort();
}

static void on_close_after_abort(EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK(error);
  print_error("close after abort", error);
  finish_abort();
}

static void on_stream_to_abort(EMSCRIPTEN_WEBTRANSPORT_T t, EMSCRIPTEN_WEBTRANSPORT_STREAM_T stream, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  aborted_stream = stream;
  CHECK_SUCCESS(emscripten_webtransport_stream_write(stream, "abc", 3, NULL, check_write_succeeded));
  CHECK_SUCCESS(emscripten_webtransport_stream_abort(stream, 7));
  // Writing and closing fail with the abort reason.
  pending_steps = 2;
  CHECK_SUCCESS(emscripten_webtransport_stream_write(stream, "def", 3, NULL, on_write_after_abort));
  CHECK_SUCCESS(emscripten_webtransport_stream_close(stream, NULL, on_close_after_abort));
}

static void on_reset_transport_ready(EMSCRIPTEN_WEBTRANSPORT_T t, const WebTransportError *error, void *userData) {
  CHECK_NO_ERROR(error);
  CHECK_SUCCESS(emscripten_webtransport_create_unidirectional_stream(t, NULL, NULL, on_stream_to_abort));
}

static void test_abort_and_cancel(void) {
  reset_transport = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  CHECK(reset_transport > 0);
  CHECK_SUCCESS(emscripten_webtransport_set_onready_callback(reset_transport, NULL, on_reset_transport_ready));
}

static void test_deinitialize(void) {
  EMSCRIPTEN_WEBTRANSPORT_T a = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  EMSCRIPTEN_WEBTRANSPORT_T b = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  CHECK(a > 0 && b > 0 && a != b);
  CHECK_SUCCESS(emscripten_webtransport_set_onclosed_callback(a, NULL, fail_on_closed));
  emscripten_webtransport_deinitialize();
  CHECK_RESULT(emscripten_webtransport_close(a, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  CHECK_RESULT(emscripten_webtransport_close(b, NULL), EMSCRIPTEN_RESULT_INVALID_TARGET);
  next_test();
}

static const Test all_tests[] = {
  {"is_supported", test_is_supported},
  {"invalid_arguments", test_invalid_arguments},
  {"rejected_connection", test_rejected_connection},
  {"certificate_hash_mismatch", test_certificate_hash_mismatch},
  {"close_while_connecting", test_close_while_connecting},
  {"connect_and_close", test_connect_and_close},
  {"supports_reliable_only", test_supports_reliable_only},
  {"invalid_options", test_invalid_options},
  {"connection_properties", test_connection_properties},
  {"open_shared_transport", test_open_shared_transport},
  {"stream_invalid_arguments", test_stream_invalid_arguments},
  {"unidirectional_stream", test_unidirectional_stream},
  {"large_unidirectional_stream", test_large_unidirectional_stream},
  {"bidirectional_stream", test_bidirectional_stream},
  {"datagrams", test_datagrams},
  {"datagrams_without_create_writable", test_datagrams_without_create_writable},
  {"close_shared_transport", test_close_shared_transport},
  {"abort_and_cancel", test_abort_and_cancel},
  {"delete_removes_callbacks", test_delete_removes_callbacks},
  {"deinitialize", test_deinitialize},
};

int main() {
  tests = all_tests;
  num_tests = sizeof(all_tests) / sizeof(all_tests[0]);
  next_test();
  // Keep the runtime alive until next_test() exits after the last test.
  emscripten_exit_with_live_runtime();
  return 0;
}
