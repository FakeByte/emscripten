#include <stdio.h>
#include <stdlib.h>
#include <emscripten/emscripten.h>
#include <emscripten/webtransport.h>
#include <assert.h>

int main() {
  assert(emscripten_webtransport_is_supported());
  printf("WebTransport is supported %d\n", emscripten_webtransport_is_supported());
  //emscripten_exit_with_live_runtime();
  emscripten_force_exit(0);
}