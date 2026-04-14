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

    class StaticFileServer : public HttpRequestHandlerFactory
    {

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

            StaticFileRequestHandler(std::string_view file_path_string);

            bool process (const HttpRequest & request, HttpResponse & response, HttpMessage::Id id) override;

        private:

            void send_file_not_found_response (HttpResponse & response);
            void send_internal_error_response (HttpResponse & response);
            bool send_file_content_response   (HttpResponse & response);

        };

        std::string base_path;

    public:

        StaticFileServer(std::string_view base_path) : base_path(base_path)
        {
        }

        HttpRequestHandler::Ptr create_handler (HttpRequest::Method method, std::string_view request_path) override
        {
            if (method == HttpRequest::GET)
            {
                using namespace std::filesystem;

                // Strip the leading '/' so that "operator /" appends rather than replaces the base path:

                std::string_view relative = request_path;

                if (not relative.empty () && relative.front () == '/') relative.remove_prefix (1);

                path full_path      = weakly_canonical (path{ base_path } / path{ relative });
                path canonical_base = weakly_canonical (path{ base_path });

                // Reject any path that resolves outside the base directory (path traversal guard):

                auto base_end = std::mismatch (canonical_base.begin (), canonical_base.end (), full_path.begin ()).first;

                if (base_end == canonical_base.end ())
                {
                    return { std::make_unique<StaticFileRequestHandler> (full_path.string ()) };
                }
            }

            return {};
        }

    };

}
