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
const Socket INVALID = (Socket)INVALID_SOCKET;
static int lastErr() { return WSAGetLastError(); }
static bool wouldBlock() {
    int e = lastErr();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
// A non-blocking connect() in progress reports WSAEWOULDBLOCK on Windows.
static bool connectPending() { return lastErr() == WSAEWOULDBLOCK; }
#else
const Socket INVALID = (Socket)-1;
static bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN; }
// A non-blocking connect() in progress reports EINPROGRESS on POSIX.
static bool connectPending() { return errno == EINPROGRESS; }
#endif

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
    ioctlsocket((SOCKET)s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

static void setNoDelay(Socket s) {
    int one = 1;
    setsockopt((
#ifdef _WIN32
                   SOCKET
#else
                   int
#endif
               )s,
               IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
}

Socket listenOn(uint16_t port) {
    Socket s = (Socket)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID) return INVALID;
    int yes = 1;
    setsockopt((
#ifdef _WIN32
                   SOCKET
#else
                   int
#endif
               )s,
               SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind((
#ifdef _WIN32
                 SOCKET
#else
                 int
#endif
             )s,
             (sockaddr*)&addr, sizeof(addr)) != 0) {
        closeSock(s);
        return INVALID;
    }
    if (listen((
#ifdef _WIN32
                   SOCKET
#else
                   int
#endif
               )s,
               8) != 0) {
        closeSock(s);
        return INVALID;
    }
    setNonBlocking(s);
    return s;
}

Socket acceptOne(Socket listener) {
    Socket c = (Socket)accept((
#ifdef _WIN32
                                  SOCKET
#else
                                  int
#endif
                              )listener,
                              nullptr, nullptr);
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
    int rc = connect((
#ifdef _WIN32
                         SOCKET
#else
                         int
#endif
                     )s,
                     (sockaddr*)&addr, sizeof(addr));
    if (rc != 0 && !connectPending()) {
        closeSock(s);
        return INVALID;
    }
    // Wait until writable (connected) or timeout.
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET((
#ifdef _WIN32
               SOCKET
#else
               int
#endif
           )s,
           &wset);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    int sel = select((int)s + 1, nullptr, &wset, nullptr, &tv);
    if (sel <= 0) {
        closeSock(s);
        return INVALID;
    }
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt((
#ifdef _WIN32
                   SOCKET
#else
                   int
#endif
               )s,
               SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err != 0) {
        closeSock(s);
        return INVALID;
    }
    setNoDelay(s);
    return s;
}

int recvSome(Socket s, char* buf, int len) {
    int n = recv((
#ifdef _WIN32
                     SOCKET
#else
                     int
#endif
                 )s,
                 buf, len, 0);
    if (n > 0) return n;
    if (n == 0) return 0;  // peer closed
    if (wouldBlock()) return -1;
    return 0;  // treat hard errors as closed
}

int sendAll(Socket s, const char* buf, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send((
#ifdef _WIN32
                         SOCKET
#else
                         int
#endif
                     )s,
                     buf + sent, len - sent, 0);
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
    closesocket((SOCKET)s);
#else
    close(s);
#endif
}

}  // namespace net
