/**
 * @license
 * Copyright 2025 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

var LibraryWebTransport = {
  $webTransport__deps: [],
  $webTransport: {
    // Internal helper object (placeholder for future state management)
    map: {}
  },

  emscripten_webtransport_is_supported__proxy: 'sync',
  emscripten_webtransport_is_supported: () => typeof WebTransport != 'undefined'
};

addToLibrary(LibraryWebTransport);
