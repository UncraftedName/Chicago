#include "ch_send.h"

void ch_send_msgpack(ch_send_ctx* ctx)
{
    BOOL success = WriteFile(ctx->pipe, ctx->mp_buf.data, ctx->mp_buf.size, NULL, NULL);
    if (!success)
        ch_clean_exit(ctx, 1);
}

// inits the msg with {"type": type, "data": ...}
int ch_msg_preamble(ch_send_ctx* ctx, ch_comm_msg_type type)
{
    msgpack_sbuffer_clear(&ctx->mp_buf);
    msgpack_packer* pk = &ctx->mp_pk;
    CH_CHK_PACK(map(pk, 2));
    CH_CHK_PACK(str_with_body(pk, CHMPK_MSG_TYPE, strlen(CHMPK_MSG_TYPE)));
    CH_CHK_PACK(int(pk, type));
    CH_CHK_PACK(str_with_body(pk, CHMPK_MSG_DATA, strlen(CHMPK_MSG_DATA)));
    return 0;
}

void ch_send_wave(ch_send_ctx* ctx, ch_comm_msg_type type)
{
    assert(type == CH_MSG_HELLO || type == CH_MSG_GOODBYE);
    if (ch_msg_preamble(ctx, type) || msgpack_pack_nil(&ctx->mp_pk))
        ch_clean_exit(ctx, 1);
    ch_send_msgpack(ctx);
}

static void ch_send_vstrf(ch_send_ctx* ctx, ch_comm_msg_type level, const char* fmt, va_list vargs)
{
    assert(level == CH_MSG_LOG_INFO || level == CH_MSG_LOG_ERROR);
    char buf[1024];
    int len = vsnprintf(buf, sizeof buf, fmt, vargs);
    len = min(sizeof(buf) - 1, len);
    if (ch_msg_preamble(ctx, level) || msgpack_pack_str_with_body(&ctx->mp_pk, buf, len))
        ch_clean_exit(ctx, 1);
    ch_send_msgpack(ctx);
}

void ch_send_log_info(ch_send_ctx* ctx, const char* fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);
    ch_send_vstrf(ctx, CH_MSG_LOG_INFO, fmt, vargs);
    va_end(vargs);
}

void ch_send_err_and_exit(ch_send_ctx* ctx, const char* fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);
    ch_send_vstrf(ctx, CH_MSG_LOG_ERROR, fmt, vargs);
    va_end(vargs);
    ch_clean_exit(ctx, 1);
}
