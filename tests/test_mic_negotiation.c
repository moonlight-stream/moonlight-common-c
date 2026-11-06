#include "NativeMicNegotiation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SDP "a=x-nv-general.clientPorts.mic:"
static unsigned tests;

static void check(int transport, const char* data, size_t length, MIC_PORT_RESULT expected, unsigned short expectedPort) {
    unsigned short port = 12345;
    MIC_PORT_RESULT result = transport ? parseMicTransportPort(data, length, &port) : parseMicSdpPort(data, length, &port);
    tests++;
    if (result != expected || port != expectedPort) {
        fprintf(stderr, "case %u: got %d/%u expected %d/%u\n", tests, result, port, expected, expectedPort);
        exit(1);
    }
}
#define S(text, result, port) check(0, text, sizeof(text)-1, result, port)
#define T(text, result, port) check(1, text, sizeof(text)-1, result, port)

int main(void) {
    check(0, NULL, 0, MIC_PORT_ABSENT, 0);
    check(0, NULL, 1, MIC_PORT_INVALID, 0);
    check(1, NULL, 0, MIC_PORT_ABSENT, 0);
    check(1, NULL, 1, MIC_PORT_INVALID, 0);
    S("", MIC_PORT_ABSENT, 0);
    S("v=0\r\na=other:42\r\n", MIC_PORT_ABSENT, 0);
    S(SDP "48002\r\n", MIC_PORT_VALID, 48002);
    S(SDP "1", MIC_PORT_VALID, 1);
    S(SDP "65535", MIC_PORT_VALID, 65535);
    S("v=0\n" SDP "\t48002 \t\n", MIC_PORT_VALID, 48002);
    S(SDP "0", MIC_PORT_DEFERRED, 0);
    S(SDP "65536", MIC_PORT_INVALID, 0);
    S(SDP "9999999999999999999999999999999", MIC_PORT_INVALID, 0);
    S(SDP "-1", MIC_PORT_INVALID, 0);
    S(SDP "+1", MIC_PORT_INVALID, 0);
    S(SDP "", MIC_PORT_INVALID, 0);
    S(SDP "42x", MIC_PORT_INVALID, 0);
    S(SDP "42 43", MIC_PORT_INVALID, 0);
    S(SDP "42\r", MIC_PORT_INVALID, 0);
    S(SDP "42\rX", MIC_PORT_INVALID, 0);
    S(SDP "42\0" SDP "43", MIC_PORT_INVALID, 0);
    S(SDP "42\n" SDP "42", MIC_PORT_INVALID, 0);
    S(SDP "42\n" SDP "43", MIC_PORT_INVALID, 0);
    S("a=other:" SDP "42", MIC_PORT_ABSENT, 0);
    // A suffix outside the declared span must not change the result.
    check(0, SDP "42garbage", sizeof(SDP "42")-1, MIC_PORT_VALID, 42);
    char rawSdp[] = {'a','=','x','-','n','v','-','g','e','n','e','r','a','l','.','c','l','i','e','n','t','P','o','r','t','s','.','m','i','c',':','4','2'};
    check(0, rawSdp, sizeof(rawSdp), MIC_PORT_VALID, 42);
    T("", MIC_PORT_ABSENT, 0);
    T("unicast", MIC_PORT_ABSENT, 0);
    T("RTP/AVP/UDP;unicast;server_port=48002-48003;source=127.0.0.1", MIC_PORT_VALID, 48002);
    T("server_port=1", MIC_PORT_VALID, 1);
    T("server_port=65535", MIC_PORT_VALID, 65535);
    T(" SERVER_PORT \t= 42-43 \t", MIC_PORT_VALID, 42);
    T("server_port=42-42", MIC_PORT_VALID, 42);
    T("server_port=0", MIC_PORT_INVALID, 0);
    T("server_port=65536", MIC_PORT_INVALID, 0);
    T("server_port=9999999999999999999999", MIC_PORT_INVALID, 0);
    T("server_port=42-65536", MIC_PORT_INVALID, 0);
    T("server_port=42-9999999999999999999", MIC_PORT_INVALID, 0);
    T("server_port=42-0", MIC_PORT_INVALID, 0);
    T("server_port=42-41", MIC_PORT_INVALID, 0);
    T("server_port=42-", MIC_PORT_INVALID, 0);
    T("server_port=42-43-44", MIC_PORT_INVALID, 0);
    T("server_port=42x", MIC_PORT_INVALID, 0);
    T("server_port=+42", MIC_PORT_INVALID, 0);
    T("server_port=-42", MIC_PORT_INVALID, 0);
    T("server_port", MIC_PORT_INVALID, 0);
    T("server_port=", MIC_PORT_INVALID, 0);
    T("server_port=\"42\"", MIC_PORT_INVALID, 0);
    T("server_port=42;server_port=42", MIC_PORT_INVALID, 0);
    T("server_port=42;SERVER_PORT=43", MIC_PORT_INVALID, 0);
    T("server_port=42,server_port=43", MIC_PORT_INVALID, 0);
    T("server_port=42\0;server_port=43", MIC_PORT_INVALID, 0);
    T("server_port=42\r\n", MIC_PORT_INVALID, 0);
    T("server_port=42;", MIC_PORT_INVALID, 0);
    T(";server_port=42", MIC_PORT_INVALID, 0);
    T("unicast;;server_port=42", MIC_PORT_INVALID, 0);
    T("xserver_port=42", MIC_PORT_ABSENT, 0);
    check(1, "server_port=42garbage", sizeof("server_port=42")-1, MIC_PORT_VALID, 42);
    char rawTransport[] = {'s','e','r','v','e','r','_','p','o','r','t','=','4','2'};
    check(1, rawTransport, sizeof(rawTransport), MIC_PORT_VALID, 42);
    if (parseMicSdpPort(SDP "42", sizeof(SDP "42")-1, NULL) != MIC_PORT_INVALID ||
            parseMicTransportPort("server_port=42", 14, NULL) != MIC_PORT_INVALID) return 1;
    puts("PASS: bounded production microphone negotiation parser tests");
    printf("%u table cases plus null-output checks\n", tests);
    return 0;
}
