#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/emscripten.h>
#include <emscripten/webtransport.h>
#include <assert.h>

#define ECHO_SERVER_URL "https://wt-ord.akaleapi.net:6161/echo"

// Test 1: Check if WebTransport is supported
void test_is_supported() {
  printf("Test 1: Checking WebTransport support...\n");
  assert(emscripten_webtransport_is_supported());
  printf("  PASS: WebTransport is supported\n");
}

// Global state for connection test
EMSCRIPTEN_WEBTRANSPORT_T g_transport = 0;
int g_poll_count = 0;
unsigned short g_last_state = EMSCRIPTEN_WEBTRANSPORT_STATE_CONNECTING;

// Polling callback to check connection state
void poll_connection_state(void* userData) {
  unsigned short state;
  EMSCRIPTEN_RESULT result;

  result = emscripten_webtransport_get_ready_state(g_transport, &state);
  assert(result == EMSCRIPTEN_RESULT_SUCCESS);

  g_poll_count++;

  // Print state transitions
  if (state != g_last_state) {
    printf("  State transition: %d -> %d (poll count: %d)\n", g_last_state, state, g_poll_count);
    g_last_state = state;
  }

  // Check what state we're in
  if (state == EMSCRIPTEN_WEBTRANSPORT_STATE_CONNECTING) {
    // Still connecting, keep polling
    if (g_poll_count < 100) {
      emscripten_async_call(poll_connection_state, NULL, 100);
    } else {
      printf("  FAIL: Connection timeout after %d polls\n", g_poll_count);
      emscripten_force_exit(1);
    }
  } else if (state == EMSCRIPTEN_WEBTRANSPORT_STATE_CONNECTED) {
    // Connected! Verify URL
    printf("  SUCCESS: Connected after %d polls\n", g_poll_count);

    // Get URL length
    int urlLength;
    result = emscripten_webtransport_get_url_length(g_transport, &urlLength);
    assert(result == EMSCRIPTEN_RESULT_SUCCESS);
    printf("  URL length: %d\n", urlLength);

    // Get URL
    char* url = (char*)malloc(urlLength);
    result = emscripten_webtransport_get_url(g_transport, url, urlLength);
    assert(result == EMSCRIPTEN_RESULT_SUCCESS);
    printf("  URL: %s\n", url);
    assert(strcmp(url, ECHO_SERVER_URL) == 0);
    free(url);

    // Close the connection
    printf("  Closing connection...\n");
    result = emscripten_webtransport_close(g_transport, 1000, "Test complete");
    assert(result == EMSCRIPTEN_RESULT_SUCCESS);

    // Continue polling to wait for CLOSED state
    emscripten_async_call(poll_connection_state, NULL, 100);
  } else if (state == EMSCRIPTEN_WEBTRANSPORT_STATE_CLOSED) {
    // Connection closed normally
    printf("  Connection closed normally\n");

    // Get close code
    unsigned short closeCode;
    result = emscripten_webtransport_get_close_code(g_transport, &closeCode);
    assert(result == EMSCRIPTEN_RESULT_SUCCESS);
    printf("  Close code: %d\n", closeCode);

    // Get close reason length
    int reasonLength;
    result = emscripten_webtransport_get_close_reason_length(g_transport, &reasonLength);
    assert(result == EMSCRIPTEN_RESULT_SUCCESS);

    // Get close reason
    if (reasonLength > 0) {
      char* reason = (char*)malloc(reasonLength);
      result = emscripten_webtransport_get_close_reason(g_transport, reason, reasonLength);
      assert(result == EMSCRIPTEN_RESULT_SUCCESS);
      printf("  Close reason: %s\n", reason);
      free(reason);
    }

    // Clean up
    result = emscripten_webtransport_delete(g_transport);
    assert(result == EMSCRIPTEN_RESULT_SUCCESS);

    printf("  PASS: Connection test completed\n");
    emscripten_force_exit(0);
  } else if (state == EMSCRIPTEN_WEBTRANSPORT_STATE_FAILED) {
    // Connection failed
    printf("  FAIL: Connection failed (state: %d)\n", state);

    // Try to get close reason
    int reasonLength;
    result = emscripten_webtransport_get_close_reason_length(g_transport, &reasonLength);
    if (result == EMSCRIPTEN_RESULT_SUCCESS && reasonLength > 0) {
      char* reason = (char*)malloc(reasonLength);
      result = emscripten_webtransport_get_close_reason(g_transport, reason, reasonLength);
      if (result == EMSCRIPTEN_RESULT_SUCCESS) {
        printf("  Failure reason: %s\n", reason);
      }
      free(reason);
    }

    emscripten_webtransport_delete(g_transport);
    emscripten_force_exit(1);
  }
}

// Test 2: Basic connection test
void test_basic_connection() {
  printf("Test 2: Basic connection to echo server...\n");

  // Initialize create attributes
  EmscriptenWebTransportCreateAttributes attrs;
  emscripten_webtransport_init_create_attributes(&attrs);
  attrs.url = ECHO_SERVER_URL;

  // Create WebTransport connection
  g_transport = emscripten_webtransport_new(&attrs);
  if (g_transport <= 0) {
    printf("  FAIL: Could not create WebTransport (error: %d)\n", g_transport);
    emscripten_force_exit(1);
    return;
  }

  printf("  Created WebTransport with handle: %d\n", g_transport);

  // Start polling for connection state
  printf("  Polling for connection state...\n");
  emscripten_async_call(poll_connection_state, NULL, 100);
}

int main() {
  printf("Starting WebTransport tests...\n\n");

  test_is_supported();
  printf("\n");

  test_basic_connection();

  // The test will exit via emscripten_force_exit() when complete
  return 0;
}