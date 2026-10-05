# Included via CMAKE_PROJECT_TOP_LEVEL_INCLUDES, after project() has
# filled CMAKE_OBJCOPY with a bare name. AnyPS5's EXISTS check needs a path.
if(DEFINED ENV{LLVM_MINGW} AND EXISTS "$ENV{LLVM_MINGW}/bin/llvm-objcopy")
    set(CMAKE_OBJCOPY "$ENV{LLVM_MINGW}/bin/llvm-objcopy" CACHE FILEPATH "MinGW objcopy" FORCE)
else()
    set(CMAKE_OBJCOPY /usr/bin/x86_64-w64-mingw32-objcopy CACHE FILEPATH "MinGW objcopy" FORCE)
endif()
