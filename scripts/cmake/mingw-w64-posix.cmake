# Linux-hosted MinGW-w64, POSIX threads, x86-64 Windows.
# Ubuntu's default alternative is the win32 thread model. AnyPS5's
# supported toolchain is WinLibs GCC 15.2.0 posix-seh; this file points
# at the distro compiler instead (GCC 13 on Ubuntu 24.04).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_OBJCOPY x86_64-w64-mingw32-objcopy CACHE FILEPATH "" FORCE)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
