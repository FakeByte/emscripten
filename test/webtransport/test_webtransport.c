#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/emscripten.h>
#include <emscripten/webtransport.h>
#include <assert.h>

#define ECHO_SERVER_URL "https://wt-ord.akaleapi.net:6161/echo"
//#define ECHO_SERVER_URL "https://127.0.0.1:4433/wt"

EM_JS(uint8_t*, base64_decode_js, (const char* b64_cstr, int* out_len), {
  const b64 = UTF8ToString(b64_cstr);

  // Requires an engine that supports Uint8Array.fromBase64()
  const bytes = Uint8Array.fromBase64(b64);

  const ptr = _malloc(bytes.length);
  HEAPU8.set(bytes, ptr);
  HEAP32[out_len >> 2] = bytes.length;

  return ptr;
});

// Test 1: Check if WebTransport is supported
void test_is_supported() {
  printf("Test 1: Checking WebTransport support...\n");
  assert(emscripten_webtransport_is_supported());
  printf("  PASS: WebTransport is supported\n");
}

// Global state for connection test
EMSCRIPTEN_WEBTRANSPORT_T g_transport = 0;
int g_onready_callback_fired = 0;
int g_onclosed_callback_fired = 0;
int g_closed_callback_code = 0;
char g_closed_callback_reason[256] = {0};

// Deferred exit callback - called after JS cleanup completes
void deferred_exit(void* userData) {
  (void)userData;
  emscripten_force_exit(0);
}

// Callback function for onclosed event
void on_webtransport_closed(EMSCRIPTEN_WEBTRANSPORT_T transport, void* userData, int closeCode, const char* reason) {
  printf("  onclosed callback fired! transport=%d, userData=%p, closeCode=%d, reason=%s\n", transport, userData, closeCode, reason ? reason : "(null)");
  g_onclosed_callback_fired = 1;
  g_closed_callback_code = closeCode;
  if (reason) {
    strncpy(g_closed_callback_reason, reason, sizeof(g_closed_callback_reason) - 1);
    g_closed_callback_reason[sizeof(g_closed_callback_reason) - 1] = '\0';
  }
  assert(transport == g_transport);
  assert(userData == (void*)0x87654321);  // Verify userData is passed correctly

  // Connection closed - verify and clean up
  printf("  Connection closed normally\n");
  printf("  PASS: onclosed callback was fired as expected\n");
  printf("  onclosed callback closeCode: %d\n", g_closed_callback_code);
  printf("  onclosed callback reason: %s\n", g_closed_callback_reason);

  // Clean up
  EMSCRIPTEN_RESULT result = emscripten_webtransport_delete(g_transport);
  assert(result == EMSCRIPTEN_RESULT_SUCCESS);

  printf("  PASS: Connection test completed\n");
  // Defer exit to allow JS code to complete cleanup (e.g., freeing memory)
  emscripten_async_call(deferred_exit, NULL, 0);
}

// Callback function for onready event
void on_webtransport_ready(EMSCRIPTEN_WEBTRANSPORT_T transport, void* userData) {
  printf("  onready callback fired! transport=%d, userData=%p\n", transport, userData);
  g_onready_callback_fired = 1;
  assert(transport == g_transport);
  assert(userData == (void*)0x12345678);  // Verify userData is passed correctly

  printf("  SUCCESS: Connected!\n");
  printf("  PASS: onready callback was fired as expected\n");

  EMSCRIPTEN_RESULT result;

  // Close the connection
  printf("  Closing connection...\n");
  WebTransportCloseInfo closeInfo;
  closeInfo.closeCode = 1000;
  closeInfo.reason = "Test complete";
  result = emscripten_webtransport_close(g_transport, &closeInfo);
  assert(result == EMSCRIPTEN_RESULT_SUCCESS);

  // The onclosed callback will be fired when the connection is closed
}

// Test 2: Basic connection test
void test_basic_connection() {
  printf("Test 2: Basic connection to echo server...\n");

  // Initialize create attributes
  WebTransportCreateOptions options;

  const char* hash = "cel9H4o2q3nSvzQ8wk7OLGRM1yNQ7fbOw6WbHvM/b1U=";

  int outputLength = 0;
  uint8_t* c = base64_decode_js(hash, &outputLength);
  assert(c && outputLength > 0);

  options.serverCertificateHashesCount = 1;
  options.serverCertificateHashes = (WebTransportHash*)malloc(sizeof(WebTransportHash));
  options.serverCertificateHashes[0].algorithm = "sha-256";
  //options.serverCertificateHashes[0].value = (void*)hash;
  options.serverCertificateHashes[0].value = c;
  //options.serverCertificateHashes[0].valueLength = strlen(hash);
  options.serverCertificateHashes[0].valueLength = (int)outputLength;

  // Create WebTransport connection
  //g_transport = emscripten_webtransport_new(ECHO_SERVER_URL, &options);
  g_transport = emscripten_webtransport_new(ECHO_SERVER_URL, NULL);
  if (g_transport <= 0) {
    printf("  FAIL: Could not create WebTransport (error: %d)\n", g_transport);
    emscripten_force_exit(1);
    return;
  }

  printf("  Created WebTransport with handle: %d\n", g_transport);

  // Register the onready callback
  printf("  Registering onready callback...\n");
  EMSCRIPTEN_RESULT result = emscripten_webtransport_set_onready_callback(g_transport, (void*)0x12345678, on_webtransport_ready);
  assert(result == EMSCRIPTEN_RESULT_SUCCESS);
  printf("  onready callback registered successfully\n");

  // Register the onclosed callback
  printf("  Registering onclosed callback...\n");
  result = emscripten_webtransport_set_onclosed_callback(g_transport, (void*)0x87654321, on_webtransport_closed);
  assert(result == EMSCRIPTEN_RESULT_SUCCESS);
  printf("  onclosed callback registered successfully\n");

  // The test will continue via callbacks
  printf("  Waiting for connection...\n");
}

int main() {
  printf("Starting WebTransport tests...\n\n");

  test_is_supported();
  printf("\n");

  test_basic_connection();

  // Keep the runtime alive until the closed callback fires and calls emscripten_force_exit()
  emscripten_exit_with_live_runtime();
  return 0;
}
