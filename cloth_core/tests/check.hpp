#pragma once
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)
#define CHECK_MSG(cond, ...)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("  FAIL %s:%d: %s -> ", __FILE__, __LINE__, #cond);      \
            std::printf(__VA_ARGS__);                                            \
            std::printf("\n");                                                   \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)
#define FINISH()                                                                 \
    do {                                                                         \
        std::printf(g_failures ? "RESULT: %d failure(s)\n" : "RESULT: ok\n", g_failures); \
        return g_failures ? 1 : 0;                                               \
    } while (0)
