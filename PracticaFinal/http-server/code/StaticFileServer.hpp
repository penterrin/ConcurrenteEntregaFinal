/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#pragma once

#include <HttpRequestHandler.hpp>
#include <HttpRequestHandlerFactory.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace argb
{

    /** This class implements a simple static file server that serves files from a specified base directory. It derives
      * from the HttpRequestHandlerFactory interface, allowing it to create request handlers for incoming HTTP requests
      * that match the criteria for serving static files.
      * The StaticFileServer class checks if the requested path corresponds to a file within the base directory and, if
      * so, creates a StaticFileRequestHandler to handle the request and serve the file content. This safely handles path
      * traversal attempts by ensuring that the resolved file path is within the base directory.
      */
    class StaticFileServer : public HttpRequestHandlerFactory
    {

        /** This class implements the logic for handling HTTP requests that correspond to static files. It manages the
          * state of the request processing, including checking for file existence, reading the file content, and generating
          * appropriate HTTP responses based on the outcome of these operations.
          */
        class StaticFileRequestHandler : public HttpRequestHandler
        {
            enum
            {
                FILE_NOT_FOUND,
                INTERNAL_ERROR,
                READING_FILE,
                FINISHED,
            }
            state;

            std::filesystem::path file_path;
            std::ifstream         reader;

        public:

            /** Constructs a StaticFileRequestHandler for the specified file path. It checks if the file exists and is a
              * regular file, opening it for reading if it is valid. The initial state of the handler is determined based
              * on the outcome of these checks, allowing it to generate appropriate responses when processing the request.
              * @param file_path_string A string view representing the absolute path to the file that this handler will serve.
              */
            StaticFileRequestHandler(std::string_view file_path_string);

            /** Processes the incoming HTTP request and generates an appropriate response. It handles different scenarios
              * such as file not found, internal errors, and successful file content delivery.
              * The method returns true when the response is fully ready to be sent back to the client, or false if the
              * handler is still processing (e.g., reading file content).
              * @param request The incoming HTTP request that triggered this handler.
              * @param response The HTTP response object that will be populated with the appropriate status, headers, and
              *     body based on the processing of the request.
              * @param id The identifier for the HTTP message being processed, which can be used for tracking or logging purposes.
              * @return true if the response is fully ready to be sent back to the client, or false if the handler is still
              *     processing.
              */
            bool process (const HttpRequest & request, HttpResponse & response, HttpMessage::Id id) override;

        private:

            void send_file_not_found_response (HttpResponse & response);
            void send_internal_error_response (HttpResponse & response);
            bool send_file_content_response   (HttpResponse & response);

        };

        std::string base_path;

    public:

        /** Constructs a StaticFileServer with the specified base directory path. The base directory is the root from
          * which files will be served.
          * @param base_path A string view representing the path to the base directory from which files will be served.
          *     This path is used as the root for resolving requested file paths.
          */
        StaticFileServer(std::string_view base_path) : base_path(base_path)
        {
        }

        /** Creates a request handler for the given HTTP method and request path. If the method is GET and the request
          * path corresponds to a file within the base directory, a StaticFileRequestHandler is created to serve the file
          * content. Otherwise, a null pointer is returned, indicating that this factory does not handle the request.
          * @param method The HTTP method of the incoming request.
          * @param request_path The path component of the incoming request URI.
          * @return A pointer to an HttpRequestHandler if the request can be handled by this factory, or a null pointer
          *     if it cannot be handled.
          */
        HttpRequestHandler::Ptr create_handler (HttpRequest::Method method, std::string_view request_path) override;

    };

}
