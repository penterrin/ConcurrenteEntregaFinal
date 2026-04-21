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
#include <vector>

// --- NUESTRA ARQUITECTURA DE CONCURRENCIA ---
#include "ThreadPool.hpp"
#include <future>
#include <thread>
#include <mutex>

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

            std::future<bool>        handler_future; // Nuestro ticket del Thread Pool

            ConnectionContext();

            ConnectionContext(const ConnectionContext&) = delete;
            ConnectionContext(ConnectionContext&& other) noexcept;

            ConnectionContext& operator = (const ConnectionContext&) = delete;
            ConnectionContext& operator = (ConnectionContext&&) noexcept = delete;
        };

        // El nuevo manager del profe (usando std::vector)
        class RequestHandlerManager
        {
            using HandlerFactoryContainer = std::vector<HttpRequestHandlerFactory*>;
            HandlerFactoryContainer handler_factories;

        public:
            void register_handler_factory(HttpRequestHandlerFactory& factory)
            {
                handler_factories.push_back(&factory);
            }

            HttpRequestHandler::Ptr create_handler(HttpRequest::Method method, std::string_view request_path) const;
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

        // --- NUESTRAS HERRAMIENTAS ---
        ThreadPool            thread_pool;
        std::mutex            connections_mutex;

    public:
        // Arrancamos el Thread Pool al crear el servidor
        HttpServer() : thread_pool(std::thread::hardware_concurrency())
        {
        }

        void register_handler_factory(HttpRequestHandlerFactory& factory)
        {
            request_handler_manager.register_handler_factory(factory);
        }

        void run(const Port& local_port)
        {
            run(Address::any, local_port);
        }

        void run(const Address& local_address, const Port& local_port);

        void stop()
        {
            running = false;
        }

    private:
        void accept_connections();
        void transfer_data();
        void receive_request(ConnectionContext& context);
        void write_response_header(ConnectionContext& context);
        void write_response_body(ConnectionContext& context);
        void run_handlers();
        void close_inactive_connections();

        // --- NUESTROS BUCLES DE HILOS ---
        void connection_management_loop();
        void data_transfer_loop();
    };
}