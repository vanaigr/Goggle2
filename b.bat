@cls
REM -DCMAKE_BUILD_TYPE=Release
REM -DCMAKE_BUILD_TYPE=Debug
@cmake -GNinja -S . -B out && ninja -C out && .\out\server.exe
