#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ev/stats.hpp"

namespace ev
{

    enum class Backend
    {
        ThreadPerConnection, // one blocking thread per connection - the baseline
        EpollLevel,          // epoll, level-triggered
        EpollEdge,           // epoll, edge-triggered
    };

    struct Config
    {
        Backend backend = Backend::EpollLevel;
        uint16_t port = 9000;
        int max_events = 256;
        size_t buffer_size = 4096;
    };

    class Server
    {
    public:
        virtual ~Server() = default;

        virtual bool start(std::string *error) = 0;

        // Runs until SIGINT/SIGTERM arrives.
        virtual void run() = 0;

        virtual StatsSnapshot stats() const = 0;
    };

    std::unique_ptr<Server> make_server(const Config &cfg);

    const char *backend_name(Backend backend);
    bool parse_backend(const std::string &text, Backend *out);

} // namespace ev
