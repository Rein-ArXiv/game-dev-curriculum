#include "network_types.h"

#if !defined(_WIN32)
#error "iocp_server.cpp is Windows-only"
#endif

#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace gnl {
namespace {

constexpr ULONG_PTR kListenKey = 1;
constexpr ULONG_PTR kDatagramKey = 2;
constexpr ULONG_PTR kShutdownKey = 3;
constexpr std::size_t kReceiveBytes = 16 * 1024;
constexpr std::size_t kDatagramBytes = 2048;

enum class OperationKind : std::uint8_t {
    Accept,
    Receive,
    Send,
    DatagramReceive,
    DatagramSend,
};

struct Session {
    ConnectionId id = 0;
    SOCKET socket = INVALID_SOCKET;
    std::atomic<bool> closing{false};
    std::mutex sendMutex;
    std::deque<std::vector<std::byte>> sendQueue;
    std::size_t queuedSendBytes = 0;
    bool sendInFlight = false;
};

struct IoOperation {
    // IOCP returns the same pointer passed to Winsock. Keep this first.
    OVERLAPPED overlapped{};
    OperationKind kind{};
    std::shared_ptr<Session> session;
    SOCKET acceptedSocket = INVALID_SOCKET;
    std::vector<std::byte> buffer;
    WSABUF wsabuf{};
    sockaddr_storage remote{};
    int remoteLength = sizeof(sockaddr_storage);
    DWORD flags = 0;
    std::size_t offset = 0;
};

Endpoint MakeEndpoint(const sockaddr* address, int length) {
    Endpoint endpoint;
    if (address == nullptr || length <= 0 ||
        static_cast<std::size_t>(length) > endpoint.storage.size()) return endpoint;
    endpoint.size = static_cast<std::uint32_t>(length);
    std::memcpy(endpoint.storage.data(), address, static_cast<std::size_t>(length));
    return endpoint;
}

const sockaddr* EndpointAddress(const Endpoint& endpoint) {
    return reinterpret_cast<const sockaddr*>(endpoint.storage.data());
}

} // namespace

class IocpServer final : public IServerBackend {
public:
    ~IocpServer() override { Stop(); }

    bool Start(const ServerConfig& config, std::string& error) override {
        if (started_) {
            error = "server already started";
            return false;
        }
        config_ = config;
        // Stop() latches stopping_ so late completions stay quiet; a new Start()
        // owns the lifetime again and has to clear it, or every PostAccept(),
        // PostReceive() and PostDatagramReceive() below bails out on the spot.
        // Cleared here rather than next to started_ because the failure paths in
        // this function call Stop(), whose own guard is that same flag.
        stopping_.store(false);

        WSADATA data{};
        int wsa = WSAStartup(MAKEWORD(2, 2), &data);
        if (wsa != 0) {
            error = "WSAStartup failed: " + std::to_string(wsa);
            return false;
        }
        wsaStarted_ = true;

        std::uint32_t concurrency = config.workerCount;
        if (concurrency == 0) {
            concurrency = std::max(1u, std::thread::hardware_concurrency());
        }
        port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, concurrency);
        if (port_ == nullptr) return FailStart("CreateIoCompletionPort", error);

        if (!CreateTcpListener(error) || !CreateUdpSocket(error)) {
            Stop();
            return false;
        }

        workerCount_ = concurrency;
        started_ = true;
        for (std::uint32_t i = 0; i < workerCount_; ++i) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }

        for (std::uint32_t i = 0; i < config_.prepostedAccepts; ++i) {
            if (!PostAccept()) {
                error = "failed to prepost AcceptEx";
                Stop();
                return false;
            }
        }
        for (std::uint32_t i = 0; i < config_.prepostedDatagrams; ++i) {
            if (!PostDatagramReceive()) {
                error = "failed to prepost WSARecvFrom";
                Stop();
                return false;
            }
        }
        return true;
    }

    std::size_t Poll(std::vector<NetworkEvent>& output,
                     std::chrono::milliseconds timeout) override {
        std::unique_lock lock(eventMutex_);
        if (events_.empty() && timeout.count() > 0) {
            eventCv_.wait_for(lock, timeout, [this] {
                return !events_.empty() || stopping_.load();
            });
        }
        std::size_t count = events_.size();
        output.reserve(output.size() + count);
        while (!events_.empty()) {
            output.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        return count;
    }

    bool SendStream(ConnectionId connection,
                    std::span<const std::byte> bytes) override {
        if (bytes.empty() || bytes.size() > std::numeric_limits<ULONG>::max()) return false;
        auto session = FindSession(connection);
        if (!session || session->closing.load()) return false;

        bool startSend = false;
        bool overflow = false;
        {
            std::lock_guard lock(session->sendMutex);
            if (session->closing.load()) return false;
            if (bytes.size() > config_.maxQueuedSendBytesPerConnection -
                                   std::min(session->queuedSendBytes,
                                            config_.maxQueuedSendBytesPerConnection)) {
                overflow = true;
            } else {
                session->sendQueue.emplace_back(bytes.begin(), bytes.end());
                session->queuedSendBytes += bytes.size();
                if (!session->sendInFlight) {
                    session->sendInFlight = true;
                    startSend = true;
                }
            }
        }
        if (overflow) {
            // Same observable result as the epoll backend: one Disconnected
            // event carrying kSendQueueOverflowError.
            CloseSession(session, kSendQueueOverflowError);
            return false;
        }
        if (startSend && !StartNextSend(session)) {
            CloseSession(session, WSAGetLastError());
            return false;
        }
        return true;
    }

    bool SendDatagram(const Endpoint& endpoint,
                      std::span<const std::byte> bytes) override {
        if (stopping_.load() || udpSocket_ == INVALID_SOCKET ||
            endpoint.size == 0 || endpoint.size > endpoint.storage.size() ||
            bytes.empty() || bytes.size() > std::numeric_limits<ULONG>::max()) return false;

        auto* operation = new IoOperation;
        operation->kind = OperationKind::DatagramSend;
        operation->buffer.assign(bytes.begin(), bytes.end());
        operation->wsabuf.buf = reinterpret_cast<char*>(operation->buffer.data());
        operation->wsabuf.len = static_cast<ULONG>(operation->buffer.size());
        std::memcpy(&operation->remote, endpoint.storage.data(), endpoint.size);
        operation->remoteLength = static_cast<int>(endpoint.size);

        DWORD sent = 0;
        AddOutstanding();
        int rc = WSASendTo(udpSocket_, &operation->wsabuf, 1, &sent, 0,
                           reinterpret_cast<const sockaddr*>(&operation->remote),
                           operation->remoteLength, &operation->overlapped, nullptr);
        if (rc == 0 || WSAGetLastError() == WSA_IO_PENDING) return true;
        RemoveOutstanding();
        delete operation;
        return false;
    }

    void Disconnect(ConnectionId connection) override {
        auto session = FindSession(connection);
        if (session) CloseSession(session, 0);
    }

    void Stop() override {
        bool expected = false;
        if (!stopping_.compare_exchange_strong(expected, true) && !started_) return;

        if (listener_ != INVALID_SOCKET) {
            closesocket(listener_);
            listener_ = INVALID_SOCKET;
        }
        if (udpSocket_ != INVALID_SOCKET) {
            closesocket(udpSocket_);
            udpSocket_ = INVALID_SOCKET;
        }

        std::vector<std::shared_ptr<Session>> sessions;
        {
            std::lock_guard lock(sessionMutex_);
            for (auto& [id, session] : sessions_) {
                static_cast<void>(id);
                sessions.push_back(session);
            }
        }
        for (const auto& session : sessions) CloseSession(session, 0);

        if (started_) {
            // snippet-begin: shutdown-drain
            std::unique_lock lock(outstandingMutex_);
            outstandingCv_.wait(lock, [this] { return outstanding_.load() == 0; });
            lock.unlock();

            for (std::uint32_t i = 0; i < workerCount_; ++i) {
                PostQueuedCompletionStatus(port_, 0, kShutdownKey, nullptr);
            }
            // snippet-end: shutdown-drain
            for (auto& worker : workers_) {
                if (worker.joinable()) worker.join();
            }
            workers_.clear();
        }

        if (port_ != nullptr) {
            CloseHandle(port_);
            port_ = nullptr;
        }
        if (wsaStarted_) {
            WSACleanup();
            wsaStarted_ = false;
        }
        started_ = false;
        eventCv_.notify_all();
    }

private:
    bool FailStart(const char* operation, std::string& error) {
        error = std::string(operation) + " failed: " + std::to_string(GetLastError());
        return false;
    }

    bool CreateTcpListener(std::string& error) {
        listener_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                               nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (listener_ == INVALID_SOCKET) {
            error = "WSASocket(TCP) failed: " + std::to_string(WSAGetLastError());
            return false;
        }

        BOOL reuse = TRUE;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr =
            htonl(config_.loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        address.sin_port = htons(config_.tcpPort);
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
                SOCKET_ERROR ||
            listen(listener_, SOMAXCONN) == SOCKET_ERROR) {
            error = "bind/listen failed: " + std::to_string(WSAGetLastError());
            return false;
        }

        if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(listener_), port_,
                                   kListenKey, 0) != port_) {
            return FailStart("associate listener", error);
        }

        GUID acceptExGuid = WSAID_ACCEPTEX;
        DWORD bytes = 0;
        if (WSAIoctl(listener_, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &acceptExGuid, sizeof(acceptExGuid),
                     &acceptEx_, sizeof(acceptEx_), &bytes, nullptr, nullptr) ==
            SOCKET_ERROR) {
            error = "load AcceptEx failed: " + std::to_string(WSAGetLastError());
            return false;
        }
        return true;
    }

    bool CreateUdpSocket(std::string& error) {
        udpSocket_ = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP,
                                nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (udpSocket_ == INVALID_SOCKET) {
            error = "WSASocket(UDP) failed: " + std::to_string(WSAGetLastError());
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr =
            htonl(config_.loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        address.sin_port = htons(config_.udpPort);
        if (bind(udpSocket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
            SOCKET_ERROR) {
            error = "bind UDP failed: " + std::to_string(WSAGetLastError());
            return false;
        }
        if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(udpSocket_), port_,
                                   kDatagramKey, 0) != port_) {
            return FailStart("associate UDP socket", error);
        }
        return true;
    }

    bool PostAccept() {
        if (stopping_.load()) return false;
        auto* operation = new IoOperation;
        operation->kind = OperationKind::Accept;
        operation->acceptedSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                                nullptr, 0, WSA_FLAG_OVERLAPPED);
        if (operation->acceptedSocket == INVALID_SOCKET) {
            delete operation;
            return false;
        }
        constexpr DWORD addressBytes = sizeof(sockaddr_storage) + 16;
        operation->buffer.resize(2 * addressBytes);

        DWORD received = 0;
        AddOutstanding();
        BOOL ok = acceptEx_(listener_, operation->acceptedSocket,
                            operation->buffer.data(), 0,
                            addressBytes, addressBytes, &received,
                            &operation->overlapped);
        if (ok || WSAGetLastError() == ERROR_IO_PENDING) return true;

        RemoveOutstanding();
        closesocket(operation->acceptedSocket);
        delete operation;
        return false;
    }

    bool PostReceive(const std::shared_ptr<Session>& session) {
        if (stopping_.load() || session->closing.load()) return false;
        auto* operation = new IoOperation;
        operation->kind = OperationKind::Receive;
        operation->session = session;
        operation->buffer.resize(kReceiveBytes);
        operation->wsabuf.buf = reinterpret_cast<char*>(operation->buffer.data());
        operation->wsabuf.len = static_cast<ULONG>(operation->buffer.size());

        DWORD received = 0;
        AddOutstanding();
        int rc = WSARecv(session->socket, &operation->wsabuf, 1, &received,
                         &operation->flags, &operation->overlapped, nullptr);
        if (rc == 0 || WSAGetLastError() == WSA_IO_PENDING) return true;
        RemoveOutstanding();
        delete operation;
        return false;
    }

    bool PostDatagramReceive() {
        if (stopping_.load()) return false;
        auto* operation = new IoOperation;
        operation->kind = OperationKind::DatagramReceive;
        operation->buffer.resize(kDatagramBytes);
        operation->wsabuf.buf = reinterpret_cast<char*>(operation->buffer.data());
        operation->wsabuf.len = static_cast<ULONG>(operation->buffer.size());

        DWORD received = 0;
        AddOutstanding();
        int rc = WSARecvFrom(udpSocket_, &operation->wsabuf, 1, &received,
                             &operation->flags,
                             reinterpret_cast<sockaddr*>(&operation->remote),
                             &operation->remoteLength,
                             &operation->overlapped, nullptr);
        if (rc == 0 || WSAGetLastError() == WSA_IO_PENDING) return true;
        RemoveOutstanding();
        delete operation;
        return false;
    }

    bool StartNextSend(const std::shared_ptr<Session>& session) {
        std::vector<std::byte> packet;
        {
            std::lock_guard lock(session->sendMutex);
            if (session->closing.load()) return false;
            if (session->sendQueue.empty()) {
                session->sendInFlight = false;
                return true;
            }
            packet = std::move(session->sendQueue.front());
            session->sendQueue.pop_front();
        }

        auto* operation = new IoOperation;
        operation->kind = OperationKind::Send;
        operation->session = session;
        operation->buffer = std::move(packet);
        return SubmitSend(operation);
    }

    bool SubmitSend(IoOperation* operation) {
        std::size_t remaining = operation->buffer.size() - operation->offset;
        operation->overlapped = {};
        operation->wsabuf.buf = reinterpret_cast<char*>(
            operation->buffer.data() + operation->offset);
        operation->wsabuf.len = static_cast<ULONG>(remaining);

        DWORD sent = 0;
        AddOutstanding();
        int rc = WSASend(operation->session->socket, &operation->wsabuf, 1,
                         &sent, 0, &operation->overlapped, nullptr);
        if (rc == 0 || WSAGetLastError() == WSA_IO_PENDING) return true;
        RemoveOutstanding();
        delete operation;
        return false;
    }

    void WorkerLoop() {
        for (;;) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            OVERLAPPED* overlapped = nullptr;
            BOOL ok = GetQueuedCompletionStatus(port_, &bytes, &key,
                                                &overlapped, INFINITE);
            if (overlapped == nullptr) {
                if (key == kShutdownKey) return;
                continue;
            }

            DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            auto* operation = reinterpret_cast<IoOperation*>(overlapped);
            RemoveOutstanding();
            DispatchCompletion(operation, bytes, error);
        }
    }

    void DispatchCompletion(IoOperation* operation, DWORD bytes, DWORD error) {
        switch (operation->kind) {
        case OperationKind::Accept:
            CompleteAccept(operation, error);
            break;
        case OperationKind::Receive:
            CompleteReceive(operation, bytes, error);
            break;
        case OperationKind::Send:
            CompleteSend(operation, bytes, error);
            break;
        case OperationKind::DatagramReceive:
            CompleteDatagramReceive(operation, bytes, error);
            break;
        case OperationKind::DatagramSend:
            delete operation;
            break;
        }
    }

    void CompleteAccept(IoOperation* operation, DWORD error) {
        SOCKET accepted = operation->acceptedSocket;
        operation->acceptedSocket = INVALID_SOCKET;
        delete operation;

        if (error == ERROR_SUCCESS && !stopping_.load()) {
            setsockopt(accepted, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                       reinterpret_cast<const char*>(&listener_), sizeof(listener_));
            BOOL noDelay = TRUE;
            setsockopt(accepted, IPPROTO_TCP, TCP_NODELAY,
                       reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

            auto session = std::make_shared<Session>();
            session->id = nextConnection_.fetch_add(1);
            session->socket = accepted;

            bool admitted = false;
            {
                std::lock_guard lock(sessionMutex_);
                if (sessions_.size() < config_.maxConnections) {
                    sessions_.emplace(session->id, session);
                    admitted = true;
                }
            }

            if (admitted &&
                CreateIoCompletionPort(reinterpret_cast<HANDLE>(accepted), port_,
                                       static_cast<ULONG_PTR>(session->id), 0) == port_) {
                PushEvent({NetworkEventKind::Connected, session->id});
                if (!PostReceive(session)) CloseSession(session, WSAGetLastError());
            } else {
                closesocket(accepted);
                if (admitted) {
                    std::lock_guard lock(sessionMutex_);
                    sessions_.erase(session->id);
                }
            }
        } else if (accepted != INVALID_SOCKET) {
            closesocket(accepted);
        }

        if (!stopping_.load()) PostAccept();
    }

    void CompleteReceive(IoOperation* operation, DWORD bytes, DWORD error) {
        auto session = operation->session;
        if (error != ERROR_SUCCESS || bytes == 0 || session->closing.load()) {
            delete operation;
            CloseSession(session, static_cast<int>(error));
            return;
        }

        NetworkEvent event;
        event.kind = NetworkEventKind::StreamBytes;
        event.connection = session->id;
        event.payload.assign(operation->buffer.begin(),
                             operation->buffer.begin() + bytes);
        PushEvent(std::move(event));
        delete operation;
        if (!PostReceive(session)) CloseSession(session, WSAGetLastError());
    }

    void CompleteSend(IoOperation* operation, DWORD bytes, DWORD error) {
        auto session = operation->session;
        if (error != ERROR_SUCCESS || bytes == 0 || session->closing.load()) {
            delete operation;
            CloseSession(session, static_cast<int>(error));
            return;
        }

        operation->offset += bytes;
        if (operation->offset < operation->buffer.size()) {
            if (!SubmitSend(operation)) CloseSession(session, WSAGetLastError());
            return;
        }

        std::size_t completedBytes = operation->buffer.size();
        delete operation;
        {
            std::lock_guard lock(session->sendMutex);
            session->queuedSendBytes -=
                std::min(session->queuedSendBytes, completedBytes);
        }
        if (!StartNextSend(session)) CloseSession(session, WSAGetLastError());
    }

    void CompleteDatagramReceive(IoOperation* operation,
                                 DWORD bytes, DWORD error) {
        if (error == ERROR_SUCCESS && !stopping_.load()) {
            NetworkEvent event;
            event.kind = NetworkEventKind::Datagram;
            event.endpoint = MakeEndpoint(
                reinterpret_cast<const sockaddr*>(&operation->remote),
                operation->remoteLength);
            event.payload.assign(operation->buffer.begin(),
                                 operation->buffer.begin() + bytes);
            PushEvent(std::move(event));
        }

        // WSAEMSGSIZE and other per-datagram errors must not consume a
        // preposted receive slot permanently. Drop the bad datagram, then
        // restore the receive depth unless shutdown owns the socket.
        if (!stopping_.load()) {
            operation->overlapped = {};
            operation->remote = {};
            operation->remoteLength = sizeof(sockaddr_storage);
            operation->flags = 0;
            DWORD received = 0;
            AddOutstanding();
            int rc = WSARecvFrom(udpSocket_, &operation->wsabuf, 1, &received,
                                 &operation->flags,
                                 reinterpret_cast<sockaddr*>(&operation->remote),
                                 &operation->remoteLength,
                                 &operation->overlapped, nullptr);
            if (rc == 0 || WSAGetLastError() == WSA_IO_PENDING) return;
            RemoveOutstanding();
        }
        delete operation;
    }

    std::shared_ptr<Session> FindSession(ConnectionId id) {
        std::lock_guard lock(sessionMutex_);
        auto it = sessions_.find(id);
        return it == sessions_.end() ? nullptr : it->second;
    }

    void CloseSession(const std::shared_ptr<Session>& session, int error) {
        if (!session || session->closing.exchange(true)) return;
        shutdown(session->socket, SD_BOTH);
        closesocket(session->socket);
        session->socket = INVALID_SOCKET;
        {
            std::lock_guard lock(sessionMutex_);
            sessions_.erase(session->id);
        }
        NetworkEvent event;
        event.kind = NetworkEventKind::Disconnected;
        event.connection = session->id;
        event.error = error;
        PushEvent(std::move(event));
    }

    void PushEvent(NetworkEvent event) {
        {
            std::lock_guard lock(eventMutex_);
            events_.push_back(std::move(event));
        }
        eventCv_.notify_one();
    }

    void AddOutstanding() { outstanding_.fetch_add(1); }

    void RemoveOutstanding() {
        if (outstanding_.fetch_sub(1) != 1) return;
        // Take the wait mutex once before notifying. Without it a Stop() thread
        // that already evaluated the predicate as false but has not registered
        // as a waiter yet would miss this notification and wait forever, because
        // stopping_ prevents any further operation from being posted.
        { std::lock_guard lock(outstandingMutex_); }
        outstandingCv_.notify_all();
    }

    ServerConfig config_{};
    HANDLE port_ = nullptr;
    SOCKET listener_ = INVALID_SOCKET;
    SOCKET udpSocket_ = INVALID_SOCKET;
    LPFN_ACCEPTEX acceptEx_ = nullptr;
    bool wsaStarted_ = false;
    bool started_ = false;
    std::atomic<bool> stopping_{false};
    std::uint32_t workerCount_ = 0;
    std::vector<std::thread> workers_;

    std::atomic<ConnectionId> nextConnection_{1};
    std::mutex sessionMutex_;
    std::unordered_map<ConnectionId, std::shared_ptr<Session>> sessions_;

    std::mutex eventMutex_;
    std::condition_variable eventCv_;
    std::deque<NetworkEvent> events_;

    std::atomic<std::uint64_t> outstanding_{0};
    std::mutex outstandingMutex_;
    std::condition_variable outstandingCv_;
};

std::unique_ptr<IServerBackend> CreatePlatformServer() {
    return std::make_unique<IocpServer>();
}

} // namespace gnl
