#include "PlatformSockets.h"
#include "Limelight-internal.h"

#define TEST_PORT_TIMEOUT_SEC 3

#define RCV_BUFFER_SIZE_MIN  32767
#define RCV_BUFFER_SIZE_STEP 16384

#define TCPv4_MSS 536
#define TCPv6_MSS 1220

void addrToUrlSafeString(struct sockaddr_storage* addr, char* string)
{
    char addrstr[INET6_ADDRSTRLEN];

    if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)addr;
        inet_ntop(addr->ss_family, &sin6->sin6_addr, addrstr, sizeof(addrstr));

        // IPv6 addresses need to be enclosed in brackets for URLs
        sprintf(string, "[%s]", addrstr);
    }
    else {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        inet_ntop(addr->ss_family, &sin->sin_addr, addrstr, sizeof(addrstr));

        // IPv4 addresses are returned without changes
        sprintf(string, "%s", addrstr);
    }
}

void shutdownTcpSocket(SOCKET s) {
    // Calling shutdown() prior to close wakes up callers
    // blocked in connect(), recv(), and friends.
    shutdown(s, SHUT_RDWR);
}

int setNonFatalRecvTimeoutMs(SOCKET s, int timeoutMs) {
#if defined(LC_WINDOWS)
    // Windows says that SO_RCVTIMEO puts the socket
    // into an indeterminate state, so we won't use
    // it for non-fatal socket operations.
    return -1;
#else
    struct timeval val;

    val.tv_sec = 0;
    val.tv_usec = timeoutMs * 1000;

    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&val, sizeof(val));
#endif
}

void setRecvTimeout(SOCKET s, int timeoutSec) {
#if defined(LC_WINDOWS)
    int val = timeoutSec * 1000;
#else
    struct timeval val;
    val.tv_sec = timeoutSec;
    val.tv_usec = 0;
#endif
    
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&val, sizeof(val)) < 0) {
        Limelog("setsockopt(SO_RCVTIMEO) failed: %d\n", (int)LastSocketError());
    }
}

int recvUdpSocket(SOCKET s, char* buffer, int size, int useSelect) {
    fd_set readfds;
    int err;
    struct timeval tv;
    
    if (useSelect) {
        FD_ZERO(&readfds);
        FD_SET(s, &readfds);

        // Wait up to 100 ms for the socket to be readable
        tv.tv_sec = 0;
        tv.tv_usec = UDP_RECV_POLL_TIMEOUT_MS * 1000;

        err = select((int)(s) + 1, &readfds, NULL, NULL, &tv);
        if (err <= 0) {
            // Return if an error or timeout occurs
            return err;
        }

        // This won't block since the socket is readable
        return (int)recv(s, buffer, size, 0);
    }
    else {
        // The caller has already configured a timeout on this
        // socket via SO_RCVTIMEO, so we can avoid a syscall
        // for each packet.
        err = (int)recv(s, buffer, size, 0);
        if (err < 0 &&
                (LastSocketError() == EWOULDBLOCK ||
                 LastSocketError() == EINTR ||
                 LastSocketError() == EAGAIN)) {
            // Return 0 for timeout
            return 0;
        }

        return err;
    }
}

void closeSocket(SOCKET s) {
#if defined(LC_WINDOWS)
    closesocket(s);
#else
    close(s);
#endif
}

SOCKET bindUdpSocket(int addrfamily, int bufferSize) {
    SOCKET s;
    struct sockaddr_storage addr;
    int err;

    LC_ASSERT(addrfamily == AF_INET || addrfamily == AF_INET6);

    s = socket(addrfamily, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        Limelog("socket() failed: %d\n", (int)LastSocketError());
        return INVALID_SOCKET;
    }

    memset(&addr, 0, sizeof(addr));
    addr.ss_family = addrfamily;
    if (bind(s, (struct sockaddr*) &addr,
        addrfamily == AF_INET ?
        sizeof(struct sockaddr_in) :
        sizeof(struct sockaddr_in6)) == SOCKET_ERROR) {
        err = LastSocketError();
        Limelog("bind() failed: %d\n", err);
        closeSocket(s);
        SetLastSocketError(err);
        return INVALID_SOCKET;
    }

#ifdef LC_DARWIN
    {
        // Disable SIGPIPE on iOS
        int val = 1;
        setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (char*)&val, sizeof(val));
    }
#endif

    // We start at the requested recv buffer value and step down until we find
    // a value that the OS will accept.
    for (;;) {
        err = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&bufferSize, sizeof(bufferSize));
        if (err == 0) {
            // Successfully set a buffer size
            break;
        }
        else if (bufferSize <= RCV_BUFFER_SIZE_MIN) {
            // Failed to set a buffer size within the allowable range
            break;
        }
        else if (bufferSize - RCV_BUFFER_SIZE_STEP <= RCV_BUFFER_SIZE_MIN) {
            // Last shot - we're trying the minimum
            bufferSize = RCV_BUFFER_SIZE_MIN;
        }
        else {
            // Lower the requested size by another step
            bufferSize -= RCV_BUFFER_SIZE_STEP;
        }
    }
    
#if defined(LC_DEBUG)
    if (err == 0) {
        Limelog("Selected receive buffer size: %d\n", bufferSize);
    }
    else {
        Limelog("Unable to set receive buffer size: %d\n", LastSocketError());
    }
#endif

    return s;
}

int setSocketNonBlocking(SOCKET s, int val) {
#if defined(__vita__)
    return setsockopt(s, SOL_SOCKET, SO_NONBLOCK, (char*)&val, sizeof(val));
#elif defined(FIONBIO)
    return ioctlsocket(s, FIONBIO, &val);
#else
    return SOCKET_ERROR;
#endif
}

SOCKET connectTcpSocket(struct sockaddr_storage* dstaddr, SOCKADDR_LEN addrlen, unsigned short port, int timeoutSec) {
    SOCKET s;
    struct sockaddr_in6 addr;
    int err;
    int nonBlocking;
    int val;

    s = socket(dstaddr->ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        Limelog("socket() failed: %d\n", (int)LastSocketError());
        return INVALID_SOCKET;
    }
    
#ifdef LC_DARWIN
    // Disable SIGPIPE on iOS
    val = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, (char*)&val, sizeof(val));
#endif

    // Some broken routers/firewalls (or routes with multiple broken routers) may result in TCP packets
    // being dropped without without us receiving an ICMP Fragmentation Needed packet. For example,
    // a router can elect to drop rather than fragment even without DF set. A misconfigured firewall
    // or router on the path back to us may block the ICMP Fragmentation Needed packet required for
    // PMTUD to work and thus we end up with a black hole route. Some OSes recover from this better
    // than others, but we can avoid the issue altogether by capping our MSS to the value mandated
    // by RFC 879 and RFC 2460.
    //
    // Note: This only changes the max packet size we can *receive* from the host PC.
    // We still must split our own sends into smaller chunks with TCP_NODELAY enabled to
    // avoid MTU issues on the way out to to the target.
#if defined(LC_WINDOWS)
    // Windows doesn't support setting TCP_MAXSEG but IP_PMTUDISC_DONT forces the MSS to the protocol
    // minimum which is what we want here. Linux doesn't do this (disabling PMTUD just avoids setting DF).
    if (dstaddr->ss_family == AF_INET) {
        val = IP_PMTUDISC_DONT;
        if (setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER, (char*)&val, sizeof(val)) < 0) {
            Limelog("setsockopt(IP_MTU_DISCOVER, IP_PMTUDISC_DONT) failed: %d\n", val, (int)LastSocketError());
        }
    }
    else {
        val = IP_PMTUDISC_DONT;
        if (setsockopt(s, IPPROTO_IPV6, IPV6_MTU_DISCOVER, (char*)&val, sizeof(val)) < 0) {
            Limelog("setsockopt(IPV6_MTU_DISCOVER, IP_PMTUDISC_DONT) failed: %d\n", val, (int)LastSocketError());
        }
    }
#elif defined(TCP_NOOPT)
    // On BSD-based OSes (including macOS/iOS), TCP_NOOPT seems to be the only way to
    // restrict MSS to the minimum. It strips all options out of the SYN packet which
    // forces the remote party to fall back to the minimum MSS. TCP_MAXSEG doesn't seem
    // to work correctly for outbound connections on macOS/iOS.
    val = 1;
    if (setsockopt(s, IPPROTO_TCP, TCP_NOOPT, (char*)&val, sizeof(val)) < 0) {
        Limelog("setsockopt(TCP_NOOPT, %d) failed: %d\n", val, (int)LastSocketError());
    }
#elif defined(TCP_MAXSEG)
    val = dstaddr->ss_family == AF_INET ? TCPv4_MSS : TCPv6_MSS;
    if (setsockopt(s, IPPROTO_TCP, TCP_MAXSEG, (char*)&val, sizeof(val)) < 0) {
        Limelog("setsockopt(TCP_MAXSEG, %d) failed: %d\n", val, (int)LastSocketError());
    }
#endif
    
    // Enable non-blocking I/O for connect timeout support
    nonBlocking = setSocketNonBlocking(s, 1) == 0;

    // Start connection
    memcpy(&addr, dstaddr, addrlen);
    addr.sin6_port = htons(port);
    err = connect(s, (struct sockaddr*) &addr, addrlen);
    if (err < 0) {
        err = (int)LastSocketError();
    }
    
    if (nonBlocking) {
        fd_set writefds, exceptfds;
        struct timeval tv;
        
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);
        FD_SET(s, &writefds);
        FD_SET(s, &exceptfds);
        
        tv.tv_sec = timeoutSec;
        tv.tv_usec = 0;
        
        // Wait for the connection to complete or the timeout to elapse
        err = select(s + 1, NULL, &writefds, &exceptfds, &tv);
        if (err < 0) {
            // select() failed
            err = LastSocketError();
            Limelog("select() failed: %d\n", err);
            closeSocket(s);
            SetLastSocketError(err);
            return INVALID_SOCKET;
        }
        else if (err == 0) {
            // select() timed out
            Limelog("select() timed out after %d seconds\n", timeoutSec);
            closeSocket(s);
            SetLastSocketError(ETIMEDOUT);
            return INVALID_SOCKET;
        }
        else if (FD_ISSET(s, &writefds) || FD_ISSET(s, &exceptfds)) {
            // The socket was signalled
            SOCKADDR_LEN len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
            if (err != 0 || FD_ISSET(s, &exceptfds)) {
                // Get the error code
                err = (err != 0) ? err : LastSocketFail();
            }
        }
        
        // Disable non-blocking I/O now that the connection is established
        setSocketNonBlocking(s, 0);
    }
    
    if (err != 0) {
        Limelog("connect() failed: %d\n", err);
        closeSocket(s);
        SetLastSocketError(err);
        return INVALID_SOCKET;
    }

    return s;
}

// See TCP_MAXSEG note in connectTcpSocket() above for more information.
// TCP_NODELAY must be enabled on the socket for this function to work!
int sendMtuSafe(SOCKET s, char* buffer, int size) {
    int bytesSent = 0;

    while (bytesSent < size) {
        int bytesToSend = size - bytesSent > TCPv4_MSS ?
                          TCPv4_MSS : size - bytesSent;

        if (send(s, &buffer[bytesSent], bytesToSend, 0) < 0) {
            return -1;
        }

        bytesSent += bytesToSend;
    }

    return bytesSent;
}

int enableNoDelay(SOCKET s) {
    int err;
    int val;

    val = 1;
    err = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&val, sizeof(val));
    if (err == SOCKET_ERROR) {
        return LastSocketError();
    }

    return 0;
}

int resolveHostName(const char* host, int family, int tcpTestPort, struct sockaddr_storage* addr, SOCKADDR_LEN* addrLen)
{
    struct addrinfo hints, *res, *currentAddr;
    int err;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    err = getaddrinfo(host, NULL, &hints, &res);
    if (err != 0) {
        Limelog("getaddrinfo(%s) failed: %d\n", host, err);
        return err;
    }
    else if (res == NULL) {
        Limelog("getaddrinfo(%s) returned success without addresses\n", host);
        return -1;
    }
    
    for (currentAddr = res; currentAddr != NULL; currentAddr = currentAddr->ai_next) {
        // Use the test port to ensure this address is working if there
        // are multiple addresses for this host name
        if (tcpTestPort != 0 && res->ai_next != NULL) {
            SOCKET testSocket = connectTcpSocket((struct sockaddr_storage*)currentAddr->ai_addr,
                                                 currentAddr->ai_addrlen,
                                                 tcpTestPort,
                                                 TEST_PORT_TIMEOUT_SEC);
            if (testSocket == INVALID_SOCKET) {
                // Try the next address
                continue;
            }
            else {
                closeSocket(testSocket);
            }
        }
        
        memcpy(addr, currentAddr->ai_addr, currentAddr->ai_addrlen);
        *addrLen = currentAddr->ai_addrlen;
        
        freeaddrinfo(res);
        return 0;
    }

    Limelog("No working addresses found for host: %s\n", host);
    freeaddrinfo(res);
    return -1;
}

int isInSubnetV6(struct sockaddr_in6* sin6, unsigned char* subnet, int prefixLength) {
    int i;
    
    for (i = 0; i < prefixLength; i++) {
        unsigned char mask = 1 << (i % 8);
        if ((sin6->sin6_addr.s6_addr[i / 8] & mask) != (subnet[i / 8] & mask)) {
            return 0;
        }
    }
    
    return 1;
}

int isPrivateNetworkAddress(struct sockaddr_storage* address) {

    // We only count IPv4 addresses as possibly private for now
    if (address->ss_family == AF_INET) {
        unsigned int addr;

        memcpy(&addr, &((struct sockaddr_in*)address)->sin_addr, sizeof(addr));
        addr = htonl(addr);
        
        // 10.0.0.0/8
        if ((addr & 0xFF000000) == 0x0A000000) {
            return 1;
        }
        // 172.16.0.0/12
        else if ((addr & 0xFFF00000) == 0xAC100000) {
            return 1;
        }
        // 192.168.0.0/16
        else if ((addr & 0xFFFF0000) == 0xC0A80000) {
            return 1;
        }
        // 169.254.0.0/16
        else if ((addr & 0xFFFF0000) == 0xA9FE0000) {
            return 1;
        }
    }
    else if (address->ss_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)address;
        static unsigned char linkLocalPrefix[] = {0xfe, 0x80};
        static unsigned char siteLocalPrefix[] = {0xfe, 0xc0};
        static unsigned char uniqueLocalPrefix[] = {0xfc, 0x00};

        // fe80::/10
        if (isInSubnetV6(sin6, linkLocalPrefix, 10)) {
            return 1;
        }
        // fec0::/10
        else if (isInSubnetV6(sin6, siteLocalPrefix, 10)) {
            return 1;
        }
        // fc00::/7
        else if (isInSubnetV6(sin6, uniqueLocalPrefix, 7)) {
            return 1;
        }
    }

    return 0;
}

int initializePlatformSockets(void) {
#if defined(LC_WINDOWS)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 0), &data);
#elif defined(__vita__)
    return 0; // already initialized
#elif defined(LC_POSIX) && !defined(LC_CHROME)
    // Disable SIGPIPE signals to avoid us getting
    // killed when a socket gets an EPIPE error
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    if (sigaction(SIGPIPE, &sa, 0) == -1) {
        perror("sigaction");
        return -1;
    }
    return 0;
#else
    return 0;
#endif
}

void cleanupPlatformSockets(void) {
#if defined(LC_WINDOWS)
    WSACleanup();
#else
#endif
}
