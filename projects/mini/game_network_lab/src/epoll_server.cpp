#include "network_types.h"

#if !defined(__linux__)
#error "epoll_server.cpp is Linux-only"
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace gnl {
namespace {

constexpr int kMaxEvents = 128;
constexpr std::size_t kReceiveBytes = 16 * 1024;
constexpr std::size_t kDatagramBytes = 2048;

enum class CommandKind : std::uint8_t {
    SendStream,
    SendDatagram,
    Disconnect,
};

struct Command {
    CommandKind kind{};
    ConnectionId connection = 0;
    Endpoint endpoint{};
    std::vector<std::byte> payload;
};

struct Session {
    ConnectionId id = 0;
    int fd = -1;
    std::deque<std::vector<std::byte>> sendQueue;
    std::size_t sendOffset = 0;
    std::size_t queuedSendBytes = 0;
};

bool SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

Endpoint MakeEndpoint(const sockaddr* address, socklen_t length) {
    Endpoint endpoint;
    if (address == nullptr || length == 0 || length > endpoint.storage.size()) return endpoint;
    endpoint.size = static_cast<std::uint32_t>(length);
    std::memcpy(endpoint.storage.data(), address, length);
    return endpoint;
}

} // namespace

class EpollServer final : public IServerBackend {
public:
    ~EpollServer() override { Stop(); }

    bool Start(const ServerConfig& config, std::string& error) override {
        if (started_) {
            error = "server already started";
            return false;
        }
        config_ = config;
        epollFd_ = epoll_create1(EPOLL_CLOEXEC);
        wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (epollFd_ < 0 || wakeFd_ < 0) {
            error = "epoll/eventfd creation failed: " + std::to_string(errno);
            Stop();
            return false;
        }
        if (!CreateListener(error) || !CreateDatagramSocket(error)) {
            Stop();
            return false;
        }
        if (!AddEpoll(wakeFd_, EPOLLIN)) {
            error = "epoll add eventfd failed: " + std::to_string(errno);
            Stop();
            return false;
        }

        stopping_.store(false);
        started_ = true;
        ioThread_ = std::thread([this] { EventLoop(); });
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
        if (stopping_.load() || bytes.empty()) return false;
        Command command;
        command.kind = CommandKind::SendStream;
        command.connection = connection;
        command.payload.assign(bytes.begin(), bytes.end());
        PushCommand(std::move(command));
        return true;
    }

    bool SendDatagram(const Endpoint& endpoint,
                      std::span<const std::byte> bytes) override {
        if (stopping_.load() || endpoint.size == 0 || bytes.empty()) return false;
        Command command;
        command.kind = CommandKind::SendDatagram;
        command.endpoint = endpoint;
        command.payload.assign(bytes.begin(), bytes.end());
        PushCommand(std::move(command));
        return true;
    }

    void Disconnect(ConnectionId connection) override {
        if (stopping_.load()) return;
        Command command;
        command.kind = CommandKind::Disconnect;
        command.connection = connection;
        PushCommand(std::move(command));
    }

    void Stop() override {
        if (!started_ && epollFd_ < 0 && wakeFd_ < 0) return;
        stopping_.store(true);
        Wake();
        if (ioThread_.joinable()) ioThread_.join();

        for (auto& [fd, session] : sessionsByFd_) {
            static_cast<void>(session);
            close(fd);
        }
        sessionsByFd_.clear();
        sessionsById_.clear();
        CloseFd(listener_);
        CloseFd(udpFd_);
        CloseFd(wakeFd_);
        CloseFd(epollFd_);
        started_ = false;
        eventCv_.notify_all();
    }

private:
    static void CloseFd(int& fd) {
        if (fd >= 0) {
            close(fd);
            fd = -1;
        }
    }

    bool CreateListener(std::string& error) {
        listener_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (listener_ < 0) {
            error = "TCP socket failed: " + std::to_string(errno);
            return false;
        }
        int reuse = 1;
        setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr =
            htonl(config_.loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        address.sin_port = htons(config_.tcpPort);
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
            listen(listener_, SOMAXCONN) < 0 || !AddEpoll(listener_, EPOLLIN)) {
            error = "TCP bind/listen failed: " + std::to_string(errno);
            return false;
        }
        return true;
    }

    bool CreateDatagramSocket(std::string& error) {
        udpFd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (udpFd_ < 0) {
            error = "UDP socket failed: " + std::to_string(errno);
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr =
            htonl(config_.loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
        address.sin_port = htons(config_.udpPort);
        if (bind(udpFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
            !AddEpoll(udpFd_, EPOLLIN)) {
            error = "UDP bind failed: " + std::to_string(errno);
            return false;
        }
        return true;
    }

    bool AddEpoll(int fd, std::uint32_t events) {
        epoll_event event{};
        event.events = events;
        event.data.fd = fd;
        return epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) == 0;
    }

    bool ModifyEpoll(int fd, std::uint32_t events) {
        epoll_event event{};
        event.events = events;
        event.data.fd = fd;
        return epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event) == 0;
    }

    void EventLoop() {
        std::array<epoll_event, kMaxEvents> events{};
        while (!stopping_.load()) {
            int count = epoll_wait(epollFd_, events.data(), kMaxEvents, -1);
            if (count < 0) {
                if (errno == EINTR) continue;
                break;
            }
            for (int i = 0; i < count; ++i) {
                int fd = events[static_cast<std::size_t>(i)].data.fd;
                std::uint32_t flags = events[static_cast<std::size_t>(i)].events;
                if (fd == wakeFd_) DrainCommands();
                else if (fd == listener_) AcceptReady();
                else if (fd == udpFd_) DatagramReady();
                else SessionReady(fd, flags);
            }
        }
        DrainCommands();
    }

    void AcceptReady() {
        for (;;) {
            sockaddr_storage address{};
            socklen_t length = sizeof(address);
            int fd = accept4(listener_, reinterpret_cast<sockaddr*>(&address), &length,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                if (errno == EINTR) continue;
                return;
            }
            if (sessionsByFd_.size() >= config_.maxConnections) {
                close(fd);
                continue;
            }
            int noDelay = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
            if (!AddEpoll(fd, EPOLLIN | EPOLLRDHUP)) {
                close(fd);
                continue;
            }
            auto session = std::make_shared<Session>();
            session->id = nextConnection_++;
            session->fd = fd;
            sessionsByFd_.emplace(fd, session);
            sessionsById_.emplace(session->id, session);
            PushEvent({NetworkEventKind::Connected, session->id});
        }
    }

    void DatagramReady() {
        std::array<std::byte, kDatagramBytes> buffer{};
        for (;;) {
            sockaddr_storage remote{};
            socklen_t length = sizeof(remote);
            ssize_t bytes = recvfrom(udpFd_, buffer.data(), buffer.size(), 0,
                                     reinterpret_cast<sockaddr*>(&remote), &length);
            if (bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                if (errno == EINTR) continue;
                return;
            }
            NetworkEvent event;
            event.kind = NetworkEventKind::Datagram;
            event.endpoint = MakeEndpoint(
                reinterpret_cast<const sockaddr*>(&remote), length);
            event.payload.assign(buffer.begin(), buffer.begin() + bytes);
            PushEvent(std::move(event));
        }
    }

    void SessionReady(int fd, std::uint32_t flags) {
        auto it = sessionsByFd_.find(fd);
        if (it == sessionsByFd_.end()) return;
        auto session = it->second;

        if ((flags & EPOLLIN) != 0) ReadSession(session);
        if (sessionsByFd_.find(fd) == sessionsByFd_.end()) return;
        if ((flags & EPOLLOUT) != 0) FlushSession(session);
        if ((flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0 &&
            sessionsByFd_.find(fd) != sessionsByFd_.end()) {
            CloseSession(session, 0);
        }
    }

    void ReadSession(const std::shared_ptr<Session>& session) {
        std::array<std::byte, kReceiveBytes> buffer{};
        for (;;) {
            ssize_t bytes = recv(session->fd, buffer.data(), buffer.size(), 0);
            if (bytes > 0) {
                NetworkEvent event;
                event.kind = NetworkEventKind::StreamBytes;
                event.connection = session->id;
                event.payload.assign(buffer.begin(), buffer.begin() + bytes);
                PushEvent(std::move(event));
                continue;
            }
            if (bytes == 0) {
                CloseSession(session, 0);
                return;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            CloseSession(session, errno);
            return;
        }
    }

    void FlushSession(const std::shared_ptr<Session>& session) {
        while (!session->sendQueue.empty()) {
            auto& packet = session->sendQueue.front();
            const std::byte* data = packet.data() + session->sendOffset;
            std::size_t remaining = packet.size() - session->sendOffset;
            ssize_t bytes = send(session->fd, data, remaining, MSG_NOSIGNAL);
            if (bytes > 0) {
                session->sendOffset += static_cast<std::size_t>(bytes);
                if (session->sendOffset == packet.size()) {
                    session->queuedSendBytes -= packet.size();
                    session->sendQueue.pop_front();
                    session->sendOffset = 0;
                }
                continue;
            }
            if (bytes < 0 && errno == EINTR) continue;
            if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                ModifyEpoll(session->fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
                return;
            }
            CloseSession(session, errno);
            return;
        }
        ModifyEpoll(session->fd, EPOLLIN | EPOLLRDHUP);
    }

    void DrainCommands() {
        std::uint64_t value = 0;
        while (read(wakeFd_, &value, sizeof(value)) < 0 && errno == EINTR) {}

        std::deque<Command> commands;
        {
            std::lock_guard lock(commandMutex_);
            commands.swap(commands_);
        }
        for (auto& command : commands) {
            if (command.kind == CommandKind::SendDatagram) {
                if (command.endpoint.size > 0) {
                    sendto(udpFd_, command.payload.data(), command.payload.size(),
                           MSG_NOSIGNAL,
                           reinterpret_cast<const sockaddr*>(command.endpoint.storage.data()),
                           static_cast<socklen_t>(command.endpoint.size));
                }
                continue;
            }

            auto it = sessionsById_.find(command.connection);
            if (it == sessionsById_.end()) continue;
            auto session = it->second;
            if (command.kind == CommandKind::Disconnect) {
                CloseSession(session, 0);
                continue;
            }
            if (command.payload.size() > config_.maxQueuedSendBytesPerConnection -
                    std::min(session->queuedSendBytes,
                             config_.maxQueuedSendBytesPerConnection)) {
                CloseSession(session, kSendQueueOverflowError);
                continue;
            }
            session->queuedSendBytes += command.payload.size();
            session->sendQueue.push_back(std::move(command.payload));
            FlushSession(session);
        }
    }

    void CloseSession(const std::shared_ptr<Session>& session, int error) {
        if (!session || session->fd < 0) return;
        int fd = session->fd;
        epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        session->fd = -1;
        sessionsByFd_.erase(fd);
        sessionsById_.erase(session->id);
        NetworkEvent event;
        event.kind = NetworkEventKind::Disconnected;
        event.connection = session->id;
        event.error = error;
        PushEvent(std::move(event));
    }

    void PushCommand(Command command) {
        {
            std::lock_guard lock(commandMutex_);
            commands_.push_back(std::move(command));
        }
        Wake();
    }

    void Wake() {
        if (wakeFd_ < 0) return;
        std::uint64_t value = 1;
        ssize_t result = write(wakeFd_, &value, sizeof(value));
        static_cast<void>(result);
    }

    void PushEvent(NetworkEvent event) {
        {
            std::lock_guard lock(eventMutex_);
            events_.push_back(std::move(event));
        }
        eventCv_.notify_one();
    }

    ServerConfig config_{};
    int epollFd_ = -1;
    int wakeFd_ = -1;
    int listener_ = -1;
    int udpFd_ = -1;
    bool started_ = false;
    std::atomic<bool> stopping_{false};
    std::thread ioThread_;
    ConnectionId nextConnection_ = 1;

    std::unordered_map<int, std::shared_ptr<Session>> sessionsByFd_;
    std::unordered_map<ConnectionId, std::shared_ptr<Session>> sessionsById_;

    std::mutex commandMutex_;
    std::deque<Command> commands_;

    std::mutex eventMutex_;
    std::condition_variable eventCv_;
    std::deque<NetworkEvent> events_;
};

std::unique_ptr<IServerBackend> CreatePlatformServer() {
    return std::make_unique<EpollServer>();
}

} // namespace gnl

