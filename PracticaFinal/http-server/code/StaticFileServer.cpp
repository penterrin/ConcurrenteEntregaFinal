
/// @copyright Copyright (c) 2026 Ángel, All rights reserved.
/// angel.rodriguez@udit.es

#include <MimeType.hpp>
#include "StaticFileServer.hpp"
#include <stdexcept>

namespace argb
{

    StaticFileServer::StaticFileRequestHandler::StaticFileRequestHandler(std::string_view file_path_string)
        : file_path(file_path_string)
    {
        if (not std::filesystem::exists (file_path) || not std::filesystem::is_regular_file (file_path))
        {
            state = FILE_NOT_FOUND;
        }
        else
        {
            reader.open (file_path, std::ios::binary);
            
            state = reader.is_open () ? READING_FILE : INTERNAL_ERROR;
        }
    }

    bool StaticFileServer::StaticFileRequestHandler::process
    (
        const HttpRequest & request,
        HttpResponse      & response,
        HttpMessage::Id     id
    )
    {
        switch (state)
        {
            case FILE_NOT_FOUND:
            {
                send_file_not_found_response (response);

                state = FINISHED;

                return true;
            }

            case INTERNAL_ERROR:
            {
                send_internal_error_response (response);

                state = FINISHED;

                return true;
            }

            case READING_FILE:
            {
                bool finished = send_file_content_response (response);

                if (finished) state = FINISHED;
            
                return finished;
            }
        }

        return true;
    }

    void StaticFileServer::StaticFileRequestHandler::send_file_not_found_response (HttpResponse & response)
    {
        static constexpr std::string_view not_found_message = "File not found";

        HttpResponse::Serializer(response)
            .status     (404)
            .header     ("Content-Type",   "text/plain; charset=utf-8")
            .header     ("Content-Length", std::to_string (not_found_message.size ()))
            .header     ("Connection",     "close")
            .end_header ()
            .body       (not_found_message);
    }

    void StaticFileServer::StaticFileRequestHandler::send_internal_error_response (HttpResponse & response)
    {
        static constexpr std::string_view internal_error_message = "Internal error";

        HttpResponse::Serializer(response)
            .status     (500)
            .header     ("Content-Type",   "text/plain; charset=utf-8")
            .header     ("Content-Length", std::to_string (internal_error_message.size ()))
            .header     ("Connection",     "close")
            .end_header ()
            .body       (internal_error_message);
    }

    bool StaticFileServer::StaticFileRequestHandler::send_file_content_response (HttpResponse & response)
    {
        // Aquí se podría implementar la lógica de lectura incremental del archivo y escritura en el cuerpo de la respuesta.
        // Por simplicidad, por el momento se lee todo el archivo de una vez.

        size_t content_length = std::filesystem::file_size (file_path);

        std::vector<char> file_content(content_length);

        reader.read (file_content.data (), content_length);

        if (not reader.good ())
        {
            state = INTERNAL_ERROR;
            return false;
        }
            
        HttpResponse::Serializer(response)
            .status     (200)
            .header     ("Content-Type",   MimeType::from (file_path.string ()).string)
            .header     ("Content-Length", std::to_string (content_length))
            .header     ("Connection",     "close")
            .end_header ()
            .body       (file_content);

        return true;
    }

}