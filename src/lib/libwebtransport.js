/**
 * @license
 * Copyright 2025 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

var LibraryWebTransport = {
  $webTransports__deps: ['$HandleAllocator'],
  $webTransports: "new HandleAllocator();",

  $WT__deps: ['$webTransports'],
  $WT: {
    getTransport(transportId) {
      if (!webTransports.has(transportId)) {
        return 0;
      }
      return webTransports.get(transportId);
    },
  },

  emscripten_webtransport_is_supported__proxy: 'sync',
  emscripten_webtransport_is_supported: () => typeof WebTransport != 'undefined',

  emscripten_webtransport_new__proxy: 'sync',
  emscripten_webtransport_new: (createAttributes) => {
    if (!globalThis.WebTransport) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    }
    if (!createAttributes) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    }

    var url = UTF8ToString({{{ makeGetValue('createAttributes', 0, '*') }}});
    var allowPooling = {{{ makeGetValue('createAttributes', C_STRUCTS.EmscriptenWebTransportCreateAttributes.allowPooling, 'i8') }}};
    var congestionControlPtr = {{{ makeGetValue('createAttributes', C_STRUCTS.EmscriptenWebTransportCreateAttributes.congestionControl, '*') }}};
    var requireUnreliable = {{{ makeGetValue('createAttributes', C_STRUCTS.EmscriptenWebTransportCreateAttributes.requireUnreliable, 'i8') }}};

    // Build options object
    var options = {};
    if (allowPooling) options.allowPooling = true;
    if (congestionControlPtr) options.congestionControl = UTF8ToString(congestionControlPtr);
    if (requireUnreliable) options.requireUnreliable = true;

    try {
      // Create the WebTransport instance
      var transport = new WebTransport(url, options);

      // Create wrapper object to track state
      var wrapper = {
        transport: transport,
        url: url,
        readyState: 0, // CONNECTING
        closeCode: 0,
        closeReason: ''
      };

      // Set up ready promise handler
      transport.ready.then(() => {
        wrapper.readyState = 1; // CONNECTED
      }).catch((e) => {
        wrapper.readyState = 3; // FAILED
        wrapper.closeReason = e.message || 'Connection failed';
      });

      // Set up closed promise handler
      transport.closed.then((closeInfo) => {
        wrapper.readyState = 2; // CLOSED
        wrapper.closeCode = closeInfo?.closeCode || 0;
        wrapper.closeReason = closeInfo?.reason || '';
      }).catch((e) => {
        wrapper.readyState = 3; // FAILED
        wrapper.closeReason = e.message || 'Connection closed abruptly';
      });

      // Allocate handle and return
      var transportId = webTransports.allocate(wrapper);

      return transportId;
    } catch (e) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_FAILED }}};
    }
  },

  emscripten_webtransport_get_ready_state__deps: ['$WT'],
  emscripten_webtransport_get_ready_state__proxy: 'sync',
  emscripten_webtransport_get_ready_state: (transportId, readyState) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }

    {{{ makeSetValue('readyState', '0', 'wrapper.readyState', 'i16') }}};
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_get_url__deps: ['$WT', '$stringToUTF8'],
  emscripten_webtransport_get_url__proxy: 'sync',
  emscripten_webtransport_get_url: (transportId, url, urlLength) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }
    if (!url) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    stringToUTF8(wrapper.url, url, urlLength);
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_get_url_length__deps: ['$WT', '$lengthBytesUTF8'],
  emscripten_webtransport_get_url_length__proxy: 'sync',
  emscripten_webtransport_get_url_length: (transportId, urlLength) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }
    if (!urlLength) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    {{{ makeSetValue('urlLength', '0', 'lengthBytesUTF8(wrapper.url)+1', 'i32') }}};
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_close__deps: ['$WT'],
  emscripten_webtransport_close__proxy: 'sync',
  emscripten_webtransport_close: (transportId, closeCode, reason) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }

    var reasonStr = reason ? UTF8ToString(reason) : undefined;

    try {
      // Build close info object
      var closeInfo = {};
      if (closeCode) closeInfo.closeCode = closeCode;
      if (reasonStr) closeInfo.reason = reasonStr;

      // Close the transport
      wrapper.transport.close(closeInfo);
      return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
    } catch (e) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_FAILED }}};
    }
  },

  emscripten_webtransport_get_close_code__deps: ['$WT'],
  emscripten_webtransport_get_close_code__proxy: 'sync',
  emscripten_webtransport_get_close_code: (transportId, closeCode) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }

    {{{ makeSetValue('closeCode', '0', 'wrapper.closeCode', 'i16') }}};
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_get_close_reason__deps: ['$WT', '$stringToUTF8'],
  emscripten_webtransport_get_close_reason__proxy: 'sync',
  emscripten_webtransport_get_close_reason: (transportId, reason, reasonLength) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }
    if (!reason) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    stringToUTF8(wrapper.closeReason, reason, reasonLength);
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_get_close_reason_length__deps: ['$WT', '$lengthBytesUTF8'],
  emscripten_webtransport_get_close_reason_length__proxy: 'sync',
  emscripten_webtransport_get_close_reason_length: (transportId, reasonLength) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }
    if (!reasonLength) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    {{{ makeSetValue('reasonLength', '0', 'lengthBytesUTF8(wrapper.closeReason)+1', 'i32') }}};
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_delete__deps: ['$WT'],
  emscripten_webtransport_delete__proxy: 'sync',
  emscripten_webtransport_delete: (transportId) => {
    var wrapper = WT.getTransport(transportId);
    if (!wrapper) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }

    webTransports.free(transportId);
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_deinitialize__deps: ['$webTransports', 'emscripten_webtransport_delete'],
  emscripten_webtransport_deinitialize__proxy: 'sync',
  emscripten_webtransport_deinitialize: () => {
    for (var i in webTransports.allocated) {
      if (webTransports.has(i)) {
        var wrapper = webTransports.get(i);
        wrapper.transport.close();
        _emscripten_webtransport_delete(i);
      }
    }
  }
};

addToLibrary(LibraryWebTransport);
