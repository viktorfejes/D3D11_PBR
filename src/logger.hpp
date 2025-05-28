#pragma once

#include <cstdio>

#ifndef LOG
    // #define LOG(message) std::cout << "[LOG] " << message << std::endl
    // #define LOG(format, ...) printf("[LOG] " format "\n", ##__VA_ARGS__)
    #define LOG(format, ...) printf("[LOG] " format "\n" __VA_OPT__(,) __VA_ARGS__)
#endif
