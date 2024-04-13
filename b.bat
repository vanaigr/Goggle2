@cls
@cmake -GNinja -S . -B out && ninja -C out && .\out\server.exe
