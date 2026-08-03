#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <cstdio>

int main(int argc, char *argv[])
{
    // The suite carries a hard CTest budget. When a case wedges, CTest kills the
    // process, and block-buffered stdout takes every case name reported so far
    // with it: the log then shows a timeout with no indication of where. Writing
    // unbuffered costs nothing at this scale and lets a wedge name itself.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    return Catch::Session().run(argc, argv);
}
