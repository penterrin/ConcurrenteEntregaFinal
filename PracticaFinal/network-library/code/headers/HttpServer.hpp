/// @copyright Copyright (c) 2026 Laura Gallego, All rights reserved.
/// laura.gallego@udit.es

#pragma once

#include <HttpRequest.hpp>
#include <HttpResponse.hpp>
#include <HttpRequestHandler.hpp>
#include <HttpRequestHandlerFactory.hpp>
#include <snippets.hpp>
#include <TcpListener.hpp>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <string_view>

// --- INCLUDES DEL THREAD POOL Y CONCURRENCIA ---
#include "ThreadPool.hpp"
#include <future>
#include <thread>
#include <mutex> // NUEVO: Para el candado del mapa de conexiones

namespace argb
{
    class HttpServer
    {
        struct ConnectionContext
        {
            enum State
            {
                RECEIVING_REQUEST,
                RUNNING_HANDLER,
                WRITING_RESPONSE_HEADER,
                WRITING_RESPONSE_BODY,
                CLOSED,
            };

            State                    state;
            Timestamp                last_activity;
            TcpSocket                socket;
            HttpRequest              request;
            HttpResponse             response;
            HttpRequest::Parser      request_parser;
            size_t                   response_bytes_sent;
            HttpRequestHandler::Ptr  handler;

            std::future<bool>        handler_future;

            ConnectionContext();

            ConnectionContext(const ConnectionContext&) = delete;
            ConnectionContext(ConnectionContext&& other) noexcept;

            ConnectionContext& operator = (const ConnectionContext&) = delete;
            ConnectionContext& operator = (ConnectionContext&&) noexcept = delete;
        };

        class RequestHandlerManager
        {
            using HandlerFactoryMap = std::map<std::string, HttpRequestHandlerFactory*, std::less<>>;
            HandlerFactoryMap handler_factories;

        public:
            void register_handler_factory(std::string path, HttpRequestHandlerFactory& factory);
            HttpRequestHandler::Ptr create_handler(HttpRequest::Method method, std::string_view request_path) const
            {
                if (auto* factory = find_handler_factory_for_path(request_path))
                {
                    return factory->create_handler(method, request_path);
                }
                return nullptr;
            }

        private:
            HttpRequestHandlerFactory* find_handler_factory_for_path(std::string_view request_path) const;
        };

        struct ListenerScopeGuard
        {
            TcpListener& listener;
            ~ListenerScopeGuard() { listener.close(true); }
        };

        using  IoBuffer = std::array<std::byte, 4096>;
        using  ConnectionMap = std::map<TcpSocket::Handle, ConnectionContext>;

        static constexpr std::chrono::seconds connection_timeout{ 10 };

    private:
        ConnectionMap         connections;
        TcpListener           listener;
        std::atomic<bool>     running{};
        RequestHandlerManager request_handler_manager;

        ThreadPool            thread_pool;

        // --- NUEVAS VARIABLES DE CONCURRENCIA ---
        std::mutex            connections_mutex; // Candado para proteger el mapa de conexiones

    public:
        HttpServer() : thread_pool(std::thread::hardware_concurrency())
        {
        }

        void register_handler_factory(std::string_view path, HttpRequestHandlerFactory& factory)
        {
            request_handler_manager.register_handler_factory(std::string(path), factory);
        }

        void run(const Port& local_port)
        {
            run(Address::any, local_port);
        }

        void run(const Address& local_address, const Port& local_port);
        void stop() { running = false; }

    private:
        void accept_connections();
        void transfer_data();
        void receive_request(ConnectionContext& context);
        void write_response_header(ConnectionContext& context);
        void write_response_body(ConnectionContext& context);
        void run_handlers();
        void close_inactive_connections();

        // --- NUEVO MÉTODO: El bucle del Recepcionista ---
        void connection_management_loop();
        void data_transfer_loop();
    };
}