#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <cstring>

#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>
#include <protocol/Parser.h>

#include <sys/epoll.h>
#include <memory>
#include <spdlog/async_logger.h>

namespace Afina {
namespace Network {
namespace STnonblock {

class Connection {
public:
    Connection(int s,std::shared_ptr<Afina::Storage>& pStorage,std::shared_ptr<spdlog::logger>& logger) : pStorage(pStorage), _logger(logger), _socket(s)
    {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        read_bytes = 0;
        written_bytes = 0;
        arg_remains = 0;

        _event.data.ptr = this;
        _event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
        alive = false;
    }

    inline bool isAlive() const { return alive; }

    void setStorage(std::shared_ptr<Afina::Storage>& pStorage);

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

    //Storage shared ptr(taking shared_ptr from main thread)
    std::shared_ptr<Afina::Storage> pStorage;

    // logger to use
    std::shared_ptr<spdlog::logger> _logger;

    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;

    std::size_t arg_remains;

    char command_buf[4096];

    int read_bytes;//read overall
    int written_bytes;//written overall

    //Read results
    std::vector<std::string> answer;

    bool alive;

    int _socket;

    struct epoll_event _event;


};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
