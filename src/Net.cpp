#include "Net.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace net {

#ifdef _WIN32
using Native = SOCKET;
const Socket INVALID = (Socket)INVALID_SOCKET;
static int lastErr() { return WSAGetLastError(); }
static bool wouldBlock() {
    int e = lastErr();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
static bool connectPending() { return lastErr() == WSAEWOULDBLOCK; }
#else
using Native = int;
const Socket INVALID = (Socket)-1;
static bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN; }
static bool connectPending() { return errno == EINPROGRESS; }
#endif

// Cast our portable Socket handle to the platform-native socket type.
static Native ns(Socket s) { return (Native)s; }

bool init() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void setNonBlocking(Socket s) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(ns(s), FIONBIO, &mode);
#else
    int flags = fcntl(ns(s), F_GETFL, 0);
    fcntl(ns(s), F_SETFL, flags | O_NONBLOCK);
#endif
}

static void setNoDelay(Socket s) {
    int one = 1;
    setsockopt(ns(s), IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

Socket listenOn(uint16_t port) {
    Socket s = (Socket)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID) return INVALID;
    int yes = 1;
    setsockopt(ns(s), SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(ns(s), (sockaddr*)&addr, sizeof(addr)) != 0) {
        closeSock(s);
        return INVALID;
    }
    if (listen(ns(s), 8) != 0) {
        closeSock(s);
        return INVALID;
    }
    setNonBlocking(s);
    return s;
}

Socket acceptOne(Socket listener) {
    Socket c = (Socket)accept(ns(listener), nullptr, nullptr);
    if (c == INVALID) return INVALID;
    setNonBlocking(c);
    setNoDelay(c);
    return c;
}

Socket connectTo(const char* host, uint16_t port, int timeoutMs) {
    Socket s = (Socket)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID) return INVALID;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        closeSock(s);
        return INVALID;
    }
    setNonBlocking(s);
    int rc = connect(ns(s), (sockaddr*)&addr, sizeof(addr));
    if (rc != 0 && !connectPending()) {
        closeSock(s);
        return INVALID;
    }
    // Wait until the socket becomes writable (connected) or we time out.
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(ns(s), &wset);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int sel = select((int)ns(s) + 1, nullptr, &wset, nullptr, &tv);
    if (sel <= 0) {
        closeSock(s);
        return INVALID;
    }
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(ns(s), SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err != 0) {
        closeSock(s);
        return INVALID;
    }
    setNoDelay(s);
    return s;
}

int recvSome(Socket s, char* buf, int len) {
    int n = recv(ns(s), buf, len, 0);
    if (n > 0) return n;
    if (n == 0) return 0;  // peer closed
    if (wouldBlock()) return -1;
    return 0;  // treat hard errors as closed
}

int sendAll(Socket s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(ns(s), buf + sent, len - sent, 0);
        if (n > 0) {
            sent += n;
        } else if (n < 0 && wouldBlock()) {
            continue;  // socket buffer full; spin briefly
        } else {
            return -1;
        }
    }
    return sent;
}

void closeSock(Socket s) {
    if (s == INVALID) return;
#ifdef _WIN32
    closesocket(ns(s));
#else
    close(ns(s));
#endif
}

}  // namespace net
