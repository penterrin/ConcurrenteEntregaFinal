
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <HttpServer.hpp>
#include <iostream>
#include <thread>

using std::cout;
using std::endl;

namespace argb
{

    HttpServer::ConnectionContext::ConnectionContext()
        : state               (RECEIVING_REQUEST)
        , last_activity       (now ()           )
        , request_parser      (request          )
        , response_bytes_sent (0                )
    {
    }

    HttpServer::ConnectionContext::ConnectionContext(ConnectionContext && other) noexcept
        : state               (other.state               )
        , last_activity       (other.last_activity       )
        , socket              (std::move (other.socket  ))
        , request             (std::move (other.request ))
        , response            (std::move (other.response))
        , request_parser      (request                   )                  // Re-initialize with this object's request
        , handler             (std::move (other.handler ))
        , response_bytes_sent (other.response_bytes_sent )
    {
    }

    void HttpServer::RequestHandlerManager::register_handler_factory (std::string path, HttpRequestHandlerFactory & factory) 
    {
        // Ensure that the path does not end with a '/' to maintain consistency in validation logic:

        if (path.length () > 1 && path.back () == '/') 
        {
            path.pop_back ();
        }

        handler_factories[std::move (path)] = &factory;
    }

    HttpRequestHandlerFactory * HttpServer::RequestHandlerManager::find_handler_factory_for_path(std::string_view request_path) const 
    {
        if (not handler_factories.empty ())
        {
            auto item = handler_factories.upper_bound (request_path);

            if (item != handler_factories.begin ()) 
            {
                --item;

                const std::string & factory_path = item->first;

                if (request_path.starts_with (factory_path))
                {                
                    // If the request path is exactly the same as the factory path, or if the next character in the
                    // request path after the factory path is a '/' (indicating a complete folder match), or if the
                    // factory path is the root "/", then we consider it a valid match:

                    if (request_path.length () == factory_path.length () || request_path[factory_path.length ()] == '/' || factory_path == "/")
                    {
                        return item->second;
                    }
                }
            }
        }

        return nullptr;
    }

    void HttpServer::run (const Address & address, const Port & port)
    {
        listener.listen (address, port);
        {
            ListenerScopeGuard guard{ listener };

            running = true;

            while (running) 
            {
                accept_connections ();
                transfer_data ();
                run_handlers ();
                close_inactive_connections ();

                std::this_thread::yield (); 
            }
        }
    }

    void HttpServer::accept_connections ()
    {
        try
        {
            while (auto new_socket = listener.accept ())
            {
                TcpSocket::Handle socket_handle = new_socket->get_handle();
            
                ConnectionContext context;

                context.socket =  std::move (*new_socket); 
                context.socket.set_blocking (false);

                connections.emplace (socket_handle, std::move (context));
            }
        }
        catch (const NetworkException & exception)
        {
            cout << "Error accepting new connection: " << exception << endl;
        }
    }

    void HttpServer::transfer_data ()
    {
        for (auto & [socket_handle, context] : connections)
        {
            try 
            {
                switch (context.state)
                {
                    case ConnectionContext::RECEIVING_REQUEST:       receive_request       (context); break;
                    case ConnectionContext::WRITING_RESPONSE_HEADER: write_response_header (context); break;
                    case ConnectionContext::WRITING_RESPONSE_BODY:   write_response_body   (context); break;
                }
            }
            catch (const NetworkException & exception)
            {
                cout << "Error during data transfer on connection " << socket_handle << ": " << exception << endl;
                context.state = ConnectionContext::CLOSED;
            }
        }
    }

    void HttpServer::receive_request (ConnectionContext & context)
    {
        IoBuffer buffer;
        size_t   received = context.socket.receive (buffer);

        if (received == TcpSocket::receive_closed) 
        {
            context.state = ConnectionContext::CLOSED;
        } 
        else
        if (received != TcpSocket::receive_empty) 
        {
            bool parsed = context.request_parser.parse ({ buffer.data (), received });

            if (parsed) 
            {
                context.handler = request_handler_manager.create_handler (context.request.get_method (), context.request.get_path ());

                if (context.handler)
                {
                    context.state = ConnectionContext::RUNNING_HANDLER;
                }
                else
                {
                    static constexpr std::string_view not_found_message = "File not found";

                    HttpResponse::Serializer(context.response)
                        .status     (404)
                        .header     ("Content-Type",   "text/plain; charset=utf-8")
                        .header     ("Content-Length", std::to_string (not_found_message.size ()))
                        .header     ("Connection",     "close")
                        .end_header ()
                        .body       (not_found_message);

                    context.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                }
            }

            context.last_activity = now ();
        }
    }

    void HttpServer::write_response_header (ConnectionContext & context)
    {
        auto   header    = std::as_bytes  (context.response.get_serialized_header ());
        auto   remaining = header.subspan (context.response_bytes_sent);

        size_t sent = context.socket.send (remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now ();
        }

        if (context.response_bytes_sent == header.size ())
        {
            context.state = ConnectionContext::WRITING_RESPONSE_BODY;
            context.response_bytes_sent = 0;
        }
    }

    void HttpServer::write_response_body (ConnectionContext & context)
    {
        auto body = std::as_bytes (context.response.get_body ());

        if (body.empty ())
        {
            context.state = ConnectionContext::CLOSED;
            return;
        }

        auto remaining = body.subspan (context.response_bytes_sent);
        
        size_t sent = context.socket.send (remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now ();
        }

        if (context.response_bytes_sent == body.size ())
        {
            context.state = ConnectionContext::CLOSED;
        }
    }

    void HttpServer::run_handlers ()
    {
        for (auto & [socket_handle, context] : connections) 
        {
            if (context.state == ConnectionContext::RUNNING_HANDLER)
            {
                if (context.handler)
                {
                    const bool finished = context.handler->process (context.request, context.response, socket_handle);

                    if (finished)
                    {
                        context.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                    }
                }
            }
        }
    }

    void HttpServer::close_inactive_connections ()
    {
        const auto current_time = now ();

        for (auto connection = connections.begin (); connection != connections.end (); )
        {
            auto & context = connection->second;
            bool   close   = false;

            if (context.state == ConnectionContext::CLOSED)
            {
                close = true;
            }
            else
            if (current_time - context.last_activity > connection_timeout)
            {
                cout << "Closing connection " << connection->first << " due to timeout." << endl;
                close = true;
            }

            if (close)
            {
                // The socket would be closed automatically when the context is destroyed, but closing it explicitly makes
                // it more clear:

                context.socket.close (); 

                connection = connections.erase (connection);
            }
            else
                ++connection;
        }
    }

}
