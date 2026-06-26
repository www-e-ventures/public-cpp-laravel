<?php
// PHP driving the compiled C++ framework through FFI — no JavaScript, no WebAssembly.
// liblipp.so is the request lifecycle (router → middleware → ORM → Blade) as a native
// shared library; PHP calls its C entry points directly.
//
//   ffi/build.sh && php -d ffi.enable=1 ffi/demo.php

$so = __DIR__ . '/dist/liblipp.so';
if (!file_exists($so)) {
    fwrite(STDERR, "missing $so — run ffi/build.sh first\n");
    exit(1);
}

$lipp = FFI::cdef(
    "const char* lipp_handle(const char*, const char*, const char*); int lipp_status();",
    $so
);

function request($lipp, string $method, string $path, string $body = ''): array {
    $ret = $lipp->lipp_handle($method, $path, $body);
    $out = is_string($ret) ? $ret : FFI::string($ret); // PHP may already hand back a string
    $status = $lipp->lipp_status();
    printf("%-5s %-16s -> HTTP %d  %s\n", $method, $path, $status, $out);
    return [$status, $out];
}

echo "PHP -> FFI -> compiled C++ framework (no JS, no WASM):\n\n";
request($lipp, 'GET', '/articles');
request($lipp, 'GET', '/articles/2');
request($lipp, 'POST', '/articles', 'title=Created from PHP&published=1');
request($lipp, 'GET', '/articles.html');
request($lipp, 'POST', '/articles', 'published=1'); // missing title -> 422
request($lipp, 'GET', '/nope');                     // unrouted -> 404
