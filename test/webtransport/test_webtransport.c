// Tests the WebTransport bindings against a public WebTransport echo server.
//
// The tests run one after another: each test starts some asynchronous
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

// Test runner

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

// Connecting that fails: the ready and closed callbacks both receive an error.

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

// Tests

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
