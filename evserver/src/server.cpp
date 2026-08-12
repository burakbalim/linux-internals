#include "ev/server.hpp"

namespace ev
{

    std::unique_ptr<Server> make_epoll_server(const Config &cfg);
    std::unique_ptr<Server> make_thread_server(const Config &cfg);

    std::unique_ptr<Server> make_server(const Config &cfg)
    {
        switch (cfg.backend)
        {
        case Backend::ThreadPerConnection:
            return make_thread_server(cfg);
        case Backend::EpollLevel:
        case Backend::EpollEdge:
            return make_epoll_server(cfg);
        }
        return nullptr;
    }

    const char *backend_name(Backend backend)
    {
        switch (backend)
        {
        case Backend::ThreadPerConnection:
            return "thread-per-connection";
        case Backend::EpollLevel:
            return "epoll (level-triggered)";
        case Backend::EpollEdge:
            return "epoll (edge-triggered)";
        }
        return "unknown";
    }

    bool parse_backend(const std::string &text, Backend *out)
    {
        if (text == "threads")
        {
            *out = Backend::ThreadPerConnection;
        }
        else if (text == "epoll")
        {
            *out = Backend::EpollLevel;
        }
        else if (text == "epoll-et")
        {
            *out = Backend::EpollEdge;
        }
        else
        {
            return false;
        }
        return true;
    }

} // namespace ev
