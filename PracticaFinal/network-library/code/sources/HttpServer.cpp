/// @copyright Copyright (c) 2026 Laura Gallego, All rights reserved.
/// laura.gallego@udit.es

#include <HttpServer.hpp>
#include <iostream>
#include <thread>

using std::cout;
using std::endl;

namespace argb
{
    HttpServer::ConnectionContext::ConnectionContext()
        : state(RECEIVING_REQUEST)
        , last_activity(now())
        , request_parser(request)
        , response_bytes_sent(0)
    {
    }

    HttpServer::ConnectionContext::ConnectionContext(ConnectionContext&& other) noexcept
        : state(other.state)
        , last_activity(other.last_activity)
        , socket(std::move(other.socket))
        , request(std::move(other.request))
        , response(std::move(other.response))
        , request_parser(request)
        , handler(std::move(other.handler))
        , handler_future(std::move(other.handler_future))
        , response_bytes_sent(other.response_bytes_sent)
    {
    }

    void HttpServer::RequestHandlerManager::register_handler_factory(std::string path, HttpRequestHandlerFactory& factory)
    {
        if (path.length() > 1 && path.back() == '/')
            path.pop_back();
        handler_factories[std::move(path)] = &factory;
    }

    HttpRequestHandlerFactory* HttpServer::RequestHandlerManager::find_handler_factory_for_path(std::string_view request_path) const
    {
        if (not handler_factories.empty())
        {
            auto item = handler_factories.upper_bound(request_path);
            if (item != handler_factories.begin())
            {
                --item;
                const std::string& factory_path = item->first;
                if (request_path.starts_with(factory_path))
                {
                    if (request_path.length() == factory_path.length() || request_path[factory_path.length()] == '/' || factory_path == "/")
                        return item->second;
                }
            }
        }
        return nullptr;
    }

    // --- EL NUEVO TRABAJO DEL RECEPCIONISTA (Hilo dedicado) ---
   // --- EL TRABAJO DEL RECEPCIONISTA (Hilo 1) ---
    void HttpServer::connection_management_loop()
    {
        while (running)
        {
            accept_connections();
            close_inactive_connections();
            std::this_thread::yield();
        }
    }

    // --- EL NUEVO TRABAJO DEL CAMARERO (Hilo 2) ---
    void HttpServer::data_transfer_loop()
    {
        while (running)
        {
            transfer_data();
            std::this_thread::yield();
        }
    }

    // --- EL JEFE DE SALA (Hilo Principal) ---
    void HttpServer::run(const Address& address, const Port& port)
    {
        listener.listen(address, port);
        {
            ListenerScopeGuard guard{ listener };
            running = true;

            // 1. Contratamos y encendemos a nuestros dos empleados especializados
            std::thread connection_thread(&HttpServer::connection_management_loop, this);
            std::thread data_thread(&HttpServer::data_transfer_loop, this); // <-- NUEVO

            // 2. El Jefe de Sala ahora solo se dedica a gestionar los tickets con la cocina
            while (running)
            {
                run_handlers();
                std::this_thread::yield();
            }

            // 3. Si cerramos el restaurante, esperamos a que los dos terminen su turno
            if (connection_thread.joinable()) connection_thread.join();
            if (data_thread.joinable())       data_thread.join(); // <-- NUEVO
        }
    }

    void HttpServer::accept_connections()
    {
        try
        {
            while (auto new_socket = listener.accept())
            {
                TcpSocket::Handle socket_handle = new_socket->get_handle();
                ConnectionContext context;
                context.socket = std::move(*new_socket);
                context.socket.set_blocking(false);

                // --- CANDADO: Protegemos la inserción de nuevos clientes ---
                std::lock_guard<std::mutex> lock(connections_mutex);
                connections.emplace(socket_handle, std::move(context));
            }
        }
        catch (const NetworkException& exception)
        {
            cout << "Error accepting new connection: " << exception << endl;
        }
    }

    void HttpServer::transfer_data()
    {
        // --- CANDADO: Protegemos la lectura de datos ---
        std::lock_guard<std::mutex> lock(connections_mutex);

        for (auto& [socket_handle, context] : connections)
        {
            try
            {
                switch (context.state)
                {
                case ConnectionContext::RECEIVING_REQUEST:       receive_request(context); break;
                case ConnectionContext::WRITING_RESPONSE_HEADER: write_response_header(context); break;
                case ConnectionContext::WRITING_RESPONSE_BODY:   write_response_body(context); break;
                }
            }
            catch (const NetworkException& exception)
            {
                cout << "Error during data transfer on connection " << socket_handle << ": " << exception << endl;
                context.state = ConnectionContext::CLOSED;
            }
        }
    }

    void HttpServer::receive_request(ConnectionContext& context)
    {
        IoBuffer buffer;
        size_t   received = context.socket.receive(buffer);

        if (received == TcpSocket::receive_closed)
        {
            context.state = ConnectionContext::CLOSED;
        }
        else if (received != TcpSocket::receive_empty)
        {
            bool parsed = context.request_parser.parse({ buffer.data(), received });

            if (parsed)
            {
                context.handler = request_handler_manager.create_handler(context.request.get_method(), context.request.get_path());

                if (context.handler)
                {
                    context.state = ConnectionContext::RUNNING_HANDLER;
                }
                else
                {
                    static constexpr std::string_view not_found_message = "File not found";
                    HttpResponse::Serializer(context.response)
                        .status(404)
                        .header("Content-Type", "text/plain; charset=utf-8")
                        .header("Content-Length", std::to_string(not_found_message.size()))
                        .header("Connection", "close")
                        .end_header()
                        .body(not_found_message);

                    context.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                }
            }
            context.last_activity = now();
        }
    }

    void HttpServer::write_response_header(ConnectionContext& context)
    {
        auto   header = std::as_bytes(context.response.get_serialized_header());
        auto   remaining = header.subspan(context.response_bytes_sent);
        size_t sent = context.socket.send(remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now();
        }

        if (context.response_bytes_sent == header.size())
        {
            context.state = ConnectionContext::WRITING_RESPONSE_BODY;
            context.response_bytes_sent = 0;
        }
    }

    void HttpServer::write_response_body(ConnectionContext& context)
    {
        auto body = std::as_bytes(context.response.get_body());

        if (body.empty())
        {
            context.state = ConnectionContext::CLOSED;
            return;
        }

        auto remaining = body.subspan(context.response_bytes_sent);
        size_t sent = context.socket.send(remaining);

        if (sent > 0)
        {
            context.response_bytes_sent += sent;
            context.last_activity = now();
        }

        if (context.response_bytes_sent == body.size())
        {
            context.state = ConnectionContext::CLOSED;
        }
    }

    void HttpServer::run_handlers()
    {
        // --- CANDADO: Protegemos el estado de las tareas ---
        std::lock_guard<std::mutex> lock(connections_mutex);

        for (auto& [socket_handle, context] : connections)
        {
            if (context.state == ConnectionContext::RUNNING_HANDLER)
            {
                if (context.handler)
                {
                    if (!context.handler_future.valid())
                    {
                        context.handler_future = thread_pool.enqueue([&context, socket_handle]() {
                            return context.handler->process(context.request, context.response, socket_handle);
                            });
                    }
                    else if (context.handler_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    {
                        const bool finished = context.handler_future.get();
                        if (finished)
                        {
                            context.state = ConnectionContext::WRITING_RESPONSE_HEADER;
                        }
                    }
                }
            }
        }
    }

    void HttpServer::close_inactive_connections()
    {
        // --- CANDADO: Protegemos el borrado de clientes ---
        std::lock_guard<std::mutex> lock(connections_mutex);

        const auto current_time = now();
        for (auto connection = connections.begin(); connection != connections.end(); )
        {
            auto& context = connection->second;
            bool   close = false;

            if (context.state == ConnectionContext::CLOSED)
                close = true;
            else if (current_time - context.last_activity > connection_timeout)
            {
                cout << "Closing connection " << connection->first << " due to timeout." << endl;
                close = true;
            }

            if (close)
            {
                context.socket.close();
                connection = connections.erase(connection);
            }
            else
                ++connection;
        }
    }
}