# havel-repl: interactive interpreter over the compositor IPC socket.
# No Qt, no wlroots: plain stdio + nlohmann/json + a unix socket.

add_executable(havel-repl main.cpp)
target_link_libraries(havel-repl PRIVATE nlohmann_json::nlohmann_json)
