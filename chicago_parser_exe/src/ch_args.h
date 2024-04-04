#pragma once

typedef enum ch_log_level {
    CH_LL_INFO,
    CH_LL_ERROR,
} ch_log_level;

#define CH_LOG_INFO(ctx, ...)             \
    do {                                  \
        if (ctx->log_level <= CH_LL_INFO) \
            fprintf(stdout, __VA_ARGS__); \
    } while (0)

#define CH_LOG_ERROR(ctx, ...)             \
    do {                                   \
        if (ctx->log_level <= CH_LL_ERROR) \
            fprintf(stderr, __VA_ARGS__);  \
    } while (0)

#define CH_LOG_LEVEL(level, ctx, ...)       \
    do {                                    \
        if (level == CH_LL_INFO)            \
            CH_LOG_INFO(ctx, __VA_ARGS__);  \
        else                                \
            CH_LOG_ERROR(ctx, __VA_ARGS__); \
    } while (0)
