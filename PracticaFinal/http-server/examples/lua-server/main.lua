-- ---------------------------------------------------------------------------
-- Setup: create and seed the test table if it does not exist yet.
-- ---------------------------------------------------------------------------

db.execute([[
    CREATE TABLE IF NOT EXISTS users (
        id    INTEGER PRIMARY KEY AUTOINCREMENT,
        name  TEXT    NOT NULL,
        email TEXT    NOT NULL UNIQUE,
        score REAL    NOT NULL DEFAULT 0
    )
]])

db.execute("DELETE FROM users")

db.execute("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {"Alice", "alice@example.com", 95.5})
db.execute("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {"Bob",   "bob@example.com",   80.0})
db.execute("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {"Eve",   "eve@example.com",   73.2})

-- ---------------------------------------------------------------------------
-- GET /hello
-- ---------------------------------------------------------------------------

server.route("GET", "/hello", function (request, response)
    local message = "Hello from the bridge! Method: " .. request:get_method()
                 .. " | Agent: " .. (request:get_header("User-Agent") or "unknown")

    response:status     (200)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  tostring(#message))
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (message)
end)

-- ---------------------------------------------------------------------------
-- GET /users
-- ---------------------------------------------------------------------------

server.route("GET", "/users", function (request, response)
    local body = ""
    local row  = db.query("SELECT id, name, email, score FROM users ORDER BY id")

    while row:advance() do
        local id    = row:get_integer (1)
        local name  = row:get_string  (2)
        local email = row:get_string  (3)
        local score = row:get_real    (4)

        body = body .. id .. " | " .. name .. " | " .. email .. " | " .. score .. "\n"
    end

    response:status     (200)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  tostring(#body))
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (body)
end)

-- ---------------------------------------------------------------------------
-- GET /users/<id>
-- ---------------------------------------------------------------------------

server.route("GET", "/users/", function (request, response)
    local path = request:get_path ()
    local id   = tonumber (path:match ("/users/(%d+)"))

    if id == nil then
        local message = "Bad request: expected /users/<integer id>"
        response:status     (400)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  tostring(#message))
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
        return
    end

    local row = db.query("SELECT name, email, score FROM users WHERE id = ?", {id})

    if row:advance() then
        local body = "name="   .. row:get_string (1)
                  .. " email=" .. row:get_string (2)
                  .. " score=" .. row:get_real   (3)

        response:status     (200)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  tostring(#body))
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (body)
    else
        local message = "User " .. id .. " not found"
        response:status     (404)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  tostring(#message))
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
    end
end)

-- ---------------------------------------------------------------------------
-- POST /users
-- ---------------------------------------------------------------------------

server.route("POST", "/users", function (request, response)
    local body  = request:get_body ()
    local name  = body:match ("name=([^&]+)")
    local email = body:match ("email=([^&]+)")
    local score = tonumber (body:match ("score=([^&]+)"))

    if name == nil or email == nil or score == nil then
        local message = "Bad request: expected body 'name=...&email=...&score=...'"
        response:status     (400)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  tostring(#message))
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
        return
    end

    db.execute("INSERT INTO users (name, email, score) VALUES (?, ?, ?)", {name, email, score})

    local row     = db.query("SELECT id FROM users WHERE email = ?", {email})
    local message

    if row:advance() then
        message = "Created user with id=" .. row:get_integer(1)
    else
        message = "Insert succeeded but could not retrieve new id"
    end

    response:status     (201)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  tostring(#message))
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (message)
end)

-- ---------------------------------------------------------------------------
-- DELETE /users/<id>
-- ---------------------------------------------------------------------------

server.route("DELETE", "/users/", function (request, response)
    local path = request:get_path ()
    local id   = tonumber (path:match ("/users/(%d+)"))

    if id == nil then
        local message = "Bad request: expected /users/<integer id>"
        response:status     (400)
        response:header     ("Content-Type",   "text/plain; charset=utf-8")
        response:header     ("Content-Length",  tostring(#message))
        response:header     ("Connection",     "close")
        response:end_header ()
        response:body       (message)
        return
    end

    db.execute("DELETE FROM users WHERE id = ?", {id})

    local message = "Deleted user " .. id

    response:status     (200)
    response:header     ("Content-Type",   "text/plain; charset=utf-8")
    response:header     ("Content-Length",  tostring(#message))
    response:header     ("Connection",     "close")
    response:end_header ()
    response:body       (message)
end)

-- ---------------------------------------------------------------------------
-- GET /async (Tareas asíncronas en Lua usando corrutinas)
-- ---------------------------------------------------------------------------

server.route("GET", "/async", function (request, response)

    local log = "Iniciando planificador de tareas (Scheduler) en Lua...\n\n"
    local tasks = {}

    local function async_worker(name, steps)
        for i = 1, steps do
            log = log .. "[Trabajador " .. name .. "] Procesando paso " .. i .. " de " .. steps .. "\n"
            coroutine.yield() -- Cede el control
        end
        log = log .. "[Trabajador " .. name .. "] ¡Ha terminado!\n"
    end

    table.insert(tasks, coroutine.create(function() async_worker("A (Corta)", 2) end))
    table.insert(tasks, coroutine.create(function() async_worker("B (Larga)", 4) end))
    table.insert(tasks, coroutine.create(function() async_worker("C (Media)", 3) end))

    local tasks_pending = true
    while tasks_pending do
        tasks_pending = false
        for _, task in ipairs(tasks) do
            if coroutine.status(task) ~= "dead" then
                tasks_pending = true
                coroutine.resume(task)
            end
        end
    end

    log = log .. "\nTodas las tareas han finalizado con exito.\n"

    response:status(200)
    response:header("Content-Type", "text/plain; charset=utf-8")
    response:header("Content-Length", tostring(#log))
    response:header("Connection", "close")
    response:end_header()
    response:body(log)
end)