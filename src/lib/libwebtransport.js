/**
 * @license
 * Copyright 2025 The Emscripten Authors
 * SPDX-License-Identifier: MIT
 */

var LibraryWebTransport = {
  // Maps handles to transport records: {transport, callbacks, streams,
  // deleted}, where `callbacks` holds the currently registered callback entry
  // for each promise of the transport and `streams` the ids of its streams.
  $webTransports__deps: ['$HandleAllocator'],
  $webTransports: 'new HandleAllocator();',

  // Maps handles to stream records: {transport, readable, writable, reader,
  // writer, buffer, deleted}. A record wraps either a ReadableStream or a
  // WritableStream of `transport` (a transport record). The reader or writer
  // is acquired on first use, `buffer` is an ArrayBuffer reused for reads.
  $webTransportStreams__deps: ['$HandleAllocator'],
  $webTransportStreams: 'new HandleAllocator();',

  $WT__deps: ['$webTransports', '$webTransportStreams', '$callUserCallback', '$withStackSave', '$stackAlloc', '$stringToUTF8OnStack', '$readI53FromI64'],
  $WT: {
    getTransport(transportId) {
      return webTransports.has(transportId) ? webTransports.get(transportId) : null;
    },

    getStream(streamId) {
      return webTransportStreams.has(streamId) ? webTransportStreams.get(streamId) : null;
    },

    allocateStream(transport, stream) {
      var streamId = webTransportStreams.allocate({transport, ...stream});
      transport.streams.add(streamId);
      return streamId;
    },

    deleteStream(streamId) {
      var stream = webTransportStreams.get(streamId);
      stream.deleted = true;
      // Rejects pending reads, whose callbacks are not called anymore.
      stream.reader?.releaseLock();
      stream.writer?.releaseLock();
      stream.transport.streams.delete(streamId);
      webTransportStreams.free(streamId);
    },

    getReader(stream) {
      return stream.reader ??= stream.readable.getReader({mode: 'byob'});
    },

    getWriter(stream) {
      return stream.writer ??= stream.writable.getWriter();
    },

    // Returns the promise returned by `fn`, or a promise rejected with the
    // exception `fn` throws.
    attempt(fn) {
      return new Promise((resolve) => resolve(fn()));
    },

    // Converts a WebTransportSendStreamOptions struct to the JS dictionary.
    readSendStreamOptions(ptr) {
      if (!ptr) return undefined;
      return {
        sendOrder: {{{ makeGetValue('ptr', C_STRUCTS.WebTransportSendStreamOptions.sendOrder, 'i53') }}},
      };
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
    // released after it returns. Without a callback the promise's outcome is
    // ignored.
    onSettled(promise, isActive, callback) {
      if (!callback) {
        promise.catch(() => {});
        return;
      }
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
    return webTransports.allocate({transport, callbacks: {}, streams: new Set()});
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
    record.deleted = true;
    record.incomingUnidirectionalReader?.releaseLock();
    for (var streamId of record.streams) WT.deleteStream(streamId);
    webTransports.free(transportId);
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_create_unidirectional_stream__deps: ['$WT'],
  emscripten_webtransport_create_unidirectional_stream__proxy: 'sync',
  emscripten_webtransport_create_unidirectional_stream: (transportId, options, userData, callback) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!callback) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    var promise = WT.attempt(() => record.transport.createUnidirectionalStream(WT.readSendStreamOptions(options)));
    WT.onSettled(promise, () => !record.deleted, (writable, error) => {
      var streamId = error ? 0 : WT.allocateStream(record, {writable});
      {{{ makeDynCall('viipp', 'callback') }}}(transportId, streamId, error, userData);
    });
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_read_incoming_unidirectional_stream__deps: ['$WT'],
  emscripten_webtransport_read_incoming_unidirectional_stream__proxy: 'sync',
  emscripten_webtransport_read_incoming_unidirectional_stream: (transportId, userData, callback) => {
    var record = WT.getTransport(transportId);
    if (!record) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!callback) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    var promise = WT.attempt(() => (record.incomingUnidirectionalReader ??= record.transport.incomingUnidirectionalStreams.getReader()).read());
    WT.onSettled(promise, () => !record.deleted, (result, error) => {
      var streamId = result?.done === false ? WT.allocateStream(record, {readable: result.value}) : 0;
      {{{ makeDynCall('viipp', 'callback') }}}(transportId, streamId, error, userData);
    });
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_stream_read__deps: ['$WT'],
  emscripten_webtransport_stream_read__proxy: 'sync',
  emscripten_webtransport_stream_read: (streamId, buffer, length, userData, callback) => {
    var stream = WT.getStream(streamId);
    if (!stream) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!stream.readable) return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    if (!buffer || !length || !callback) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    // A BYOB read cannot fill the Wasm memory directly since it transfers the
    // buffer it reads into, so it reads into a JS buffer which is then copied
    // and reused by the next read.
    var scratch = stream.buffer?.byteLength >= length ? stream.buffer : new ArrayBuffer(length);
    stream.buffer = null;
    var promise = WT.attempt(() => WT.getReader(stream).read(new Uint8Array(scratch, 0, length)));
    WT.onSettled(promise, () => !stream.deleted, (result, error) => {
      var numBytes = 0;
      // A read that was pending when the stream got canceled has no value.
      if (result?.value) {
        stream.buffer = result.value.buffer;
        numBytes = result.value.length;
        HEAPU8.set(result.value, buffer);
      }
      {{{ makeDynCall('vipipp', 'callback') }}}(streamId, numBytes, !!result?.done, error, userData);
    });
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_stream_write__deps: ['$WT'],
  emscripten_webtransport_stream_write__proxy: 'sync',
  emscripten_webtransport_stream_write: (streamId, data, length, userData, callback) => {
    var stream = WT.getStream(streamId);
    if (!stream) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!stream.writable) return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    if (!data && length) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_PARAM }}};
    // Copy the data so that the caller can reuse its buffer right away.
    var chunk = HEAPU8.slice(data, data + length);
    var promise = WT.attempt(() => WT.getWriter(stream).write(chunk));
    WT.onSettled(promise, () => !stream.deleted, callback && ((value, error) => {
      {{{ makeDynCall('vipp', 'callback') }}}(streamId, error, userData);
    }));
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_stream_close__deps: ['$WT'],
  emscripten_webtransport_stream_close__proxy: 'sync',
  emscripten_webtransport_stream_close: (streamId, userData, callback) => {
    var stream = WT.getStream(streamId);
    if (!stream) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!stream.writable) return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    var promise = WT.attempt(() => WT.getWriter(stream).close());
    WT.onSettled(promise, () => !stream.deleted, callback && ((value, error) => {
      {{{ makeDynCall('vipp', 'callback') }}}(streamId, error, userData);
    }));
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_stream_abort__deps: ['$WT'],
  emscripten_webtransport_stream_abort__proxy: 'sync',
  emscripten_webtransport_stream_abort: (streamId, streamErrorCode) => {
    var stream = WT.getStream(streamId);
    if (!stream) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!stream.writable) return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    var reason = new WebTransportError({message: 'Stream aborted', streamErrorCode});
    WT.onSettled(WT.attempt(() => WT.getWriter(stream).abort(reason)));
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_stream_cancel__deps: ['$WT'],
  emscripten_webtransport_stream_cancel__proxy: 'sync',
  emscripten_webtransport_stream_cancel: (streamId, streamErrorCode) => {
    var stream = WT.getStream(streamId);
    if (!stream) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    if (!stream.readable) return {{{ cDefs.EMSCRIPTEN_RESULT_NOT_SUPPORTED }}};
    var reason = new WebTransportError({message: 'Stream canceled', streamErrorCode});
    WT.onSettled(WT.attempt(() => WT.getReader(stream).cancel(reason)));
    return {{{ cDefs.EMSCRIPTEN_RESULT_SUCCESS }}};
  },

  emscripten_webtransport_stream_delete__deps: ['$WT'],
  emscripten_webtransport_stream_delete__proxy: 'sync',
  emscripten_webtransport_stream_delete: (streamId) => {
    if (!WT.getStream(streamId)) return {{{ cDefs.EMSCRIPTEN_RESULT_INVALID_TARGET }}};
    WT.deleteStream(streamId);
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
