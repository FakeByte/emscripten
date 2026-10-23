/**
 * @license
 * Copyright 2025 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

var LibraryWebTransport = {
  // Maps handles to {transport, callbacks}, where `callbacks` holds the
  // currently registered callback entry for each promise of the transport.
  $webTransports__deps: ['$HandleAllocator'],
  $webTransports: 'new HandleAllocator();',

  $WT__deps: ['$webTransports', '$callUserCallback', '$withStackSave', '$stackAlloc', '$stringToUTF8OnStack'],
  $WT: {
    getTransport(transportId) {
      return webTransports.has(transportId) ? webTransports.get(transportId) : null;
    },

    // Maps an exception thrown synchronously by the WebTransport API to an
    // EMSCRIPTEN_RESULT.
    resultFromException(e) {
      switch (e?.name) {
        case 'NotSupportedError': return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
        case 'SyntaxError':
        case 'TypeError':
        case 'RangeError': return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
        default: return {{{ cDefs.EMSCRIPTEN_RESULT_FAILED }}};
      }
    },

    // Writes `e`, the reason a promise was rejected with, as a
    // WebTransportError struct to the stack and returns its address.
    errorOnStack(e) {
      var ptr = stackAlloc({{{ C_STRUCTS.WebTransportError.__size__ }}});
      var source = {
        'stream': {{{ cDefs.WEBTRANSPORT_ERROR_SOURCE_STREAM }}},
        'session': {{{ cDefs.WEBTRANSPORT_ERROR_SOURCE_SESSION }}},
      }[e?.source] ?? {{{ cDefs.WEBTRANSPORT_ERROR_SOURCE_NONE }}};
      {{{ makeSetValue('ptr', C_STRUCTS.WebTransportError.name, 'stringToUTF8OnStack(e?.name ?? "Error")', '*') }}};
      {{{ makeSetValue('ptr', C_STRUCTS.WebTransportError.message, 'stringToUTF8OnStack(e?.message ?? String(e))', '*') }}};
      {{{ makeSetValue('ptr', C_STRUCTS.WebTransportError.source, 'source', 'i32') }}};
      {{{ makeSetValue('ptr', C_STRUCTS.WebTransportError.streamErrorCode, 'e?.streamErrorCode ?? -1', 'i32') }}};
      return ptr;
    },

    // Calls `callback(value, errorPtr)` from the event loop once `promise`
    // settles, unless `isActive()` returns false by then. errorPtr is 0 if the
    // promise fulfilled. Anything the callback allocates on the stack is
    // released after it returns.
    onSettled(promise, isActive, callback) {
      var settle = (value, rejected, reason) => {
        if (!isActive()) return;
        callUserCallback(() => withStackSave(() => callback(value, rejected ? WT.errorOnStack(reason) : 0)));
      };
      promise.then((value) => settle(value, false), (reason) => settle(undefined, true, reason));
    },

    // Registers `invoke(value, errorPtr)` to run when the transport's promise
    // named `name` settles, replacing the previously registered callback.
    setPromiseCallback(transportId, name, callback, invoke) {
      var record = WT.getTransport(transportId);
      if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
      if (!record.transport[name]) return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
      var entry = callback ? {} : null;
      record.callbacks[name] = entry;
      if (entry) WT.onSettled(record.transport[name], () => record.callbacks[name] === entry, invoke);
      return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
    },

    // Indexed by the WebTransportCongestionControl enum.
    congestionControls: ['default', 'throughput', 'low-latency'],
    // Indexed by the WebTransportReliabilityMode enum.
    reliabilityModes: ['pending', 'reliable-only', 'supports-unreliable'],

    // Converts a WebTransportOptions struct to the JS options dictionary.
    // Throws a TypeError for invalid enum values.
    readOptions(ptr) {
      if (!ptr) return undefined;
      var congestionControl = WT.congestionControls[{{{ makeGetValue('ptr', C_STRUCTS.WebTransportOptions.congestionControl, 'i32') }}}];
      if (!congestionControl) throw new TypeError('invalid congestionControl');
      var options = {
        allowPooling: !!{{{ makeGetValue('ptr', C_STRUCTS.WebTransportOptions.allowPooling, 'i8') }}},
        requireUnreliable: !!{{{ makeGetValue('ptr', C_STRUCTS.WebTransportOptions.requireUnreliable, 'i8') }}},
        congestionControl,
      };
      var protocols = {{{ makeGetValue('ptr', C_STRUCTS.WebTransportOptions.protocols, '*') }}};
      var protocolsCount = {{{ makeGetValue('ptr', C_STRUCTS.WebTransportOptions.protocolsCount, '*') }}};
      if (protocolsCount) {
        options.protocols = [];
        for (var i = 0; i < protocolsCount; i++) {
          var protocol = protocols + i * {{{ POINTER_SIZE }}};
          options.protocols.push(UTF8ToString({{{ makeGetValue('protocol', 0, '*') }}}));
        }
      }
      var hashes = {{{ makeGetValue('ptr', C_STRUCTS.WebTransportOptions.serverCertificateHashes, '*') }}};
      var hashesCount = {{{ makeGetValue('ptr', C_STRUCTS.WebTransportOptions.serverCertificateHashesCount, '*') }}};
      if (hashesCount) {
        options.serverCertificateHashes = [];
        for (var i = 0; i < hashesCount; i++) {
          var hash = hashes + i * {{{ C_STRUCTS.WebTransportHash.__size__ }}};
          var value = {{{ makeGetValue('hash', C_STRUCTS.WebTransportHash.value, '*') }}};
          var valueLength = {{{ makeGetValue('hash', C_STRUCTS.WebTransportHash.valueLength, '*') }}};
          options.serverCertificateHashes.push({
            algorithm: UTF8ToString({{{ makeGetValue('hash', C_STRUCTS.WebTransportHash.algorithm, '*') }}}),
            value: HEAPU8.slice(value, value + valueLength),
          });
        }
      }
      return options;
    },
  },

  emscripten_webtransport_is_supported__proxy: 'sync',
  emscripten_webtransport_is_supported: () => typeof WebTransport != 'undefined',

  emscripten_webtransport_supports_reliable_only__proxy: 'sync',
  emscripten_webtransport_supports_reliable_only: () => !!globalThis.WebTransport?.supportsReliableOnly,

  emscripten_webtransport_new__deps: ['$WT'],
  emscripten_webtransport_new__proxy: 'sync',
  emscripten_webtransport_new: (url, options) => {
    if (typeof WebTransport == 'undefined') return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    if (!url) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    try {
      var transport = new WebTransport(UTF8ToString(url), WT.readOptions(options));
    } catch (e) {
      return WT.resultFromException(e);
    }
    return webTransports.allocate({transport, callbacks: {}});
  },

  emscripten_webtransport_set_onready_callback__deps: ['$WT'],
  emscripten_webtransport_set_onready_callback__proxy: 'sync',
  emscripten_webtransport_set_onready_callback: (transportId, userData, callback) =>
    WT.setPromiseCallback(transportId, 'ready', callback, (value, error) => {
      {{{ makeDynCall('vipp', 'callback') }}}(transportId, error, userData);
    }),

  emscripten_webtransport_set_onclosed_callback__deps: ['$WT'],
  emscripten_webtransport_set_onclosed_callback__proxy: 'sync',
  emscripten_webtransport_set_onclosed_callback: (transportId, userData, callback) =>
    WT.setPromiseCallback(transportId, 'closed', callback, (closeInfo, error) => {
      var closeInfoPtr = 0;
      if (!error) {
        closeInfoPtr = stackAlloc({{{ C_STRUCTS.WebTransportCloseInfo.__size__ }}});
        {{{ makeSetValue('closeInfoPtr', C_STRUCTS.WebTransportCloseInfo.closeCode, 'closeInfo.closeCode', 'u32') }}};
        {{{ makeSetValue('closeInfoPtr', C_STRUCTS.WebTransportCloseInfo.reason, 'stringToUTF8OnStack(closeInfo.reason)', '*') }}};
      }
      {{{ makeDynCall('vippp', 'callback') }}}(transportId, closeInfoPtr, error, userData);
    }),

  emscripten_webtransport_set_ondraining_callback__deps: ['$WT'],
  emscripten_webtransport_set_ondraining_callback__proxy: 'sync',
  emscripten_webtransport_set_ondraining_callback: (transportId, userData, callback) =>
    WT.setPromiseCallback(transportId, 'draining', callback, () => {
      {{{ makeDynCall('vip', 'callback') }}}(transportId, userData);
    }),

  emscripten_webtransport_get_reliability__deps: ['$WT'],
  emscripten_webtransport_get_reliability__proxy: 'sync',
  emscripten_webtransport_get_reliability: (transportId, reliability) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    {{{ makeSetValue('reliability', 0, 'WT.reliabilityModes.indexOf(record.transport.reliability)', 'i32') }}};
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_get_congestion_control__deps: ['$WT'],
  emscripten_webtransport_get_congestion_control__proxy: 'sync',
  emscripten_webtransport_get_congestion_control: (transportId, congestionControl) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    {{{ makeSetValue('congestionControl', 0, 'WT.congestionControls.indexOf(record.transport.congestionControl)', 'i32') }}};
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_get_protocol__deps: ['$WT', '$stringToUTF8'],
  emscripten_webtransport_get_protocol__proxy: 'sync',
  emscripten_webtransport_get_protocol: (transportId, protocol, protocolLength) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!protocol) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    stringToUTF8(record.transport.protocol ?? '', protocol, protocolLength);
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_get_protocol_length__deps: ['$WT', '$lengthBytesUTF8'],
  emscripten_webtransport_get_protocol_length__proxy: 'sync',
  emscripten_webtransport_get_protocol_length: (transportId, protocolLength) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!protocolLength) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    {{{ makeSetValue('protocolLength', 0, 'lengthBytesUTF8(record.transport.protocol ?? "") + 1', 'i32') }}};
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_close__deps: ['$WT'],
  emscripten_webtransport_close__proxy: 'sync',
  emscripten_webtransport_close: (transportId, closeInfo) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    var info;
    if (closeInfo) {
      var reason = {{{ makeGetValue('closeInfo', C_STRUCTS.WebTransportCloseInfo.reason, '*') }}};
      info = {
        closeCode: {{{ makeGetValue('closeInfo', C_STRUCTS.WebTransportCloseInfo.closeCode, 'u32') }}},
        reason: reason ? UTF8ToString(reason) : '',
      };
    }
    try {
      record.transport.close(info);
    } catch (e) {
      return WT.resultFromException(e);
    }
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_delete__deps: ['$WT'],
  emscripten_webtransport_delete__proxy: 'sync',
  emscripten_webtransport_delete: (transportId) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    // Deactivates all pending callbacks, see setPromiseCallback().
    record.callbacks = {};
    webTransports.free(transportId);
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_deinitialize__deps: ['$webTransports', 'emscripten_webtransport_delete'],
  emscripten_webtransport_deinitialize__proxy: 'sync',
  emscripten_webtransport_deinitialize: () => {
    for (var id = 1; id < webTransports.allocated.length; id++) {
      if (webTransports.has(id)) {
        try {
          webTransports.get(id).transport.close();
        } catch (e) {
          // Already closed or failed.
        }
        _emscripten_webtransport_delete(id);
      }
    }
  },
};

addToLibrary(LibraryWebTransport);
