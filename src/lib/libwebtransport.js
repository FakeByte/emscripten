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
  emscripten_webtransport_new: (url, createOptions) => {
    if (!globalThis.WebTransport) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    }

    let urlString = UTF8ToString(url);

    let options = {};
    if (createOptions) {
      options.serverCertificateHashes = [];
      let hashesCount = {{{ makeGetValue('createOptions', C_STRUCTS.WebTransportCreateOptions.serverCertificateHashesCount, 'i32') }}};
      let hashes = {{{ makeGetValue('createOptions', C_STRUCTS.WebTransportCreateOptions.serverCertificateHashes, '*') }}};

      while (hashesCount--) {
        let valuePtr = {{{ makeGetValue('hashes', C_STRUCTS.WebTransportHash.value, '*') }}};
        let valueLength = {{{ makeGetValue('hashes', C_STRUCTS.WebTransportHash.valueLength, 'i32') }}};
        options.serverCertificateHashes.push({
          algorithm: UTF8ToString({{{ makeGetValue('hashes', C_STRUCTS.WebTransportHash.algorithm, '*') }}}),
          value: {{{ makeHEAPView('U8', 'valuePtr', 'valuePtr+valueLength') }}}
        });
        hashes += {{{ C_STRUCTS.WebTransportHash.__size__ }}};
      }
    }

    // Create the WebTransport instance
    let transport = new WebTransport(urlString, options);

    return webTransports.allocate(transport);
  },

  emscripten_webtransport_set_onready_callback__deps: ['$WT'],
  emscripten_webtransport_set_onready_callback__proxy: 'sync',
  emscripten_webtransport_set_onready_callback: (transportId, userData, callbackFunc) => {
    var transport = WT.getTransport(transportId);
    if (!transport) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }

    transport.ready.then(() => {
      out("WebTransport ready fired");

      {{{ makeDynCall('vip', 'callbackFunc') }}}(transportId, userData);
    });

    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_set_onclosed_callback__deps: ['$WT', '$stringToNewUTF8', 'free'],
  emscripten_webtransport_set_onclosed_callback__proxy: 'sync',
  emscripten_webtransport_set_onclosed_callback: (transportId, userData, callbackFunc) => {
    var transport = WT.getTransport(transportId);
    if (!transport) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }

    transport.closed.then((closeInfo) => {
      out("WebTransport closed fired");
      var closeCode = closeInfo?.closeCode || 0;
      var reason = closeInfo?.reason || '';
      var reasonPtr = stringToNewUTF8(reason);
      {{{ makeDynCall('vipip', 'callbackFunc') }}}(transportId, userData, closeCode, reasonPtr);
      _free(reasonPtr);
    }).catch((e) => {
      out("WebTransport closed with error: " + e.message);
      var reasonPtr = stringToNewUTF8(e.message || 'Connection closed abruptly');
      {{{ makeDynCall('vipip', 'callbackFunc') }}}(transportId, userData, 0, reasonPtr);
      _free(reasonPtr);
    });

    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_close__deps: ['$WT'],
  emscripten_webtransport_close__proxy: 'sync',
  emscripten_webtransport_close: (transportId, closeCode, reason) => {
    var transport = WT.getTransport(transportId);
    if (!transport) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    }

    var reasonStr = reason ? UTF8ToString(reason) : undefined;

    try {
      // Build close info object
      var closeInfo = {};
      if (closeCode) closeInfo.closeCode = closeCode;
      if (reasonStr) closeInfo.reason = reasonStr;

      // Close the transport
      transport.close(closeInfo);
      return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
    } catch (e) {
      return {{{ cDefs.EMSCRIPTEN_RESULT_FAILED }}};
    }
  },

  emscripten_webtransport_delete__deps: ['$WT'],
  emscripten_webtransport_delete__proxy: 'sync',
  emscripten_webtransport_delete: (transportId) => {
    var transport = WT.getTransport(transportId);
    if (!transport) {
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
        var transport = webTransports.get(i);
        transport.close();
        _emscripten_webtransport_delete(i);
      }
    }
  }
};

addToLibrary(LibraryWebTransport);
