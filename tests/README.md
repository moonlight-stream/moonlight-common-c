# Microphone negotiation parser tests

These tests compile the production `src/NativeMicNegotiation.h` directly. They require
only a C11 compiler, without ENet, OpenSSL, networking, or audio capture.

From the repository root:

```sh
cc -std=c11 -Wall -Wextra -Werror -I src tests/test_mic_negotiation.c -o test-mic-negotiation
./test-mic-negotiation
```

Clang/GCC users can additionally enable `-fsanitize=address,undefined -g`.
The checks use explicit failure returns and remain active in release builds.

Alternatively:

```sh
cmake -S tests -B build-mic-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-mic-tests
ctest --test-dir build-mic-tests --output-on-failure
```

`MIC_PORT_ABSENT` and `MIC_PORT_INVALID` are distinct so the caller can apply a
deliberate fallback policy. Invalid input always clears the output port. Both
functions accept bounded spans and reject embedded NUL bytes. SDP accepts CRLF
or LF line endings and a final line without a terminator. Transport accepts
a decimal port or an ascending decimal range, including optional surrounding
horizontal whitespace; duplicate `server_port` parameters, comma-separated
alternatives, malformed ranges, and invalid port numbers are rejected.

The RTSP caller must reject duplicate Transport headers and check SETUP status
before using these parsers. A parsed port alone establishes neither microphone
encryption nor a functioning audio endpoint.
