#include <psp2/net/net.h>
#include "PlatformSockets.h"
#include "Limelight-internal.h"

#define RCV_BUFFER_SIZE_MIN  32767
#define RCV_BUFFER_SIZE_STEP 16384

void addrToUrlSafeString(void* addr, char* string)
{
#ifdef __vita__
    char addrstr[48];
    struct sockaddr_in* sin = (struct sockaddr_in*)addr;
    sceNetInetNtop(sin->sin_family, &sin->sin_addr, addrstr, sizeof(addrstr));
    if (sin->sin_family == AF_INET6) {
        sprintf(string, "[%s]", addrstr);
    } else {
        sprintf(string, "%s", addrstr);
    }
#else
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
#endif
}

void shutdownTcpSocket(SOCKET s) {
    // Calling shutdown() prior to close wakes up callers
    // blocked in connect(), recv(), and friends.
    shutdown(s, SHUT_RDWR);
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

int recvUdpSocket(SOCKET s, char* buffer, int size) {
    fd_set readfds;
    int err;
    struct timeval tv;
    
    FD_ZERO(&readfds);
    FD_SET(s, &readfds);
    
    // Wait up to 500 ms for the socket to be readable
    tv.tv_sec = 0;
    tv.tv_usec = 500 * 1000;
    
    err = select((int)(s) + 1, &readfds, NULL, NULL, &tv);
    if (err <= 0) {
        // Return if an error or timeout occurs
        return err;
    }
    
    // This won't block since the socket is readable
    return (int)recv(s, buffer, size, 0);
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
#ifdef __vita__
    addrfamily = AF_INET;
#endif

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

SOCKET connectTcpSocket(struct sockaddr_storage* dstaddr, SOCKADDR_LEN addrlen, unsigned short port, int timeoutSec) {
    SOCKET s;
    struct sockaddr_in6 addr;
    int err;
#if defined(LC_DARWIN) || defined(FIONBIO)
    int val;
#endif

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
    
#ifdef FIONBIO
    // Enable non-blocking I/O for connect timeout support
    val = 1;
    ioctlsocket(s, FIONBIO, &val);
#endif

    // Start connection
    memcpy(&addr, dstaddr, sizeof(addr));
    addr.sin6_port = htons(port);
    err = connect(s, (struct sockaddr*) &addr, addrlen);
    if (err < 0) {
        err = (int)LastSocketError();
    }
    
#ifdef FIONBIO
    {
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
#if defined(LC_WINDOWS)
            SetLastSocketError(WSAEWOULDBLOCK);
#else
            SetLastSocketError(EWOULDBLOCK);
#endif
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
        val = 0;
        ioctlsocket(s, FIONBIO, &val);
    }
#endif
    
    if (err != 0) {
        Limelog("connect() failed: %d\n", err);
        closeSocket(s);
        SetLastSocketError(err);
        return INVALID_SOCKET;
    }

    return s;
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

int initializePlatformSockets(void) {
#if defined(__vita__)
    return 0; // already initialized
#elif defined(LC_WINDOWS)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 0), &data);
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
