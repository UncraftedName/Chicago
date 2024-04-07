#include "ch_send.h"

#define CH_CHK(expr)        \
    do {                    \
        int _mp_ret = expr; \
        if (_mp_ret != 0)   \
            return _mp_ret; \
    } while (0)

// checked msgpack pack
#define CH_CHK_PACK(x) CH_CHK(msgpack_pack_##x)

#define CH_CHK_PACK_CSTR(pk, str) CH_CHK_PACK(str_with_body(pk, str, strlen(str)))

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
    CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TYPE);
    CH_CHK_PACK(int(pk, type));
    CH_CHK_PACK_CSTR(pk, CHMPK_MSG_DATA);
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

static inline int ch_pack_module_offset(ch_send_datamap_cb_info* info, ch_ptr ptr)
{
    if (ptr) {
        size_t off = CH_PTR_DIFF(ptr, info->sc->mods[info->mod_idx].base);
        return msgpack_pack_uint64(&info->send_ctx->mp_pk, off);
    } else {
        return msgpack_pack_nil(&info->send_ctx->mp_pk);
    }
}

static int ch_recurse_pack_dm(ch_send_datamap_cb_info* info, const datamap_t* dm)
{
    msgpack_packer* pk = &info->send_ctx->mp_pk;
    if (!dm)
        return msgpack_pack_nil(pk);
    CH_CHK_PACK(map(pk, 5));
    CH_CHK_PACK_CSTR(pk, CHMPK_MSG_DM_NAME);
    CH_CHK_PACK_CSTR(pk, dm->dataClassName);
    CH_CHK_PACK_CSTR(pk, CHMPK_MSG_DM_MODULE);
    CH_CHK_PACK_CSTR(pk, ch_mod_names[info->mod_idx]);
    CH_CHK_PACK_CSTR(pk, CHMPK_MSG_DM_MODULE_OFF);
    CH_CHK(ch_pack_module_offset(info, (ch_ptr)dm));
    CH_CHK_PACK_CSTR(pk, CHMPK_MSG_DM_BASE);
    CH_CHK(ch_recurse_pack_dm(info, dm->baseMap));

    CH_CHK_PACK_CSTR(pk, CHMPK_MSG_DM_FIELDS);

    typedescription_t empty_desc = {0};
    if (dm->dataNumFields == 1 && !memcmp(&empty_desc, &dm->dataDesc[0], sizeof(typedescription_t))) {
        // single empty field, this actually means no fields
        CH_CHK_PACK(array(pk, 0));
    } else {
        CH_CHK_PACK(array(pk, dm->dataNumFields));
        for (int i = 0; i < dm->dataNumFields; i++) {
            CH_CHK_PACK(map(pk, 10));
            const typedescription_t* desc = &dm->dataDesc[i];
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_NAME);
            CH_CHK_PACK_CSTR(pk, desc->fieldName);
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_TYPE);
            CH_CHK_PACK(int(pk, desc->fieldType));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_FLAGS);
            CH_CHK_PACK(int(pk, desc->flags));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_OFF);
            CH_CHK_PACK(int(pk, desc->fieldOffset[0]));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_TOTAL_SIZE);
            CH_CHK_PACK(int(pk, desc->fieldSizeInBytes));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_RESTORE_OPS);
            CH_CHK(ch_pack_module_offset(info, desc->pSaveRestoreOps));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_INPUT_FUNC);
            CH_CHK(ch_pack_module_offset(info, desc->inputFunc));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_EMBEDDED);
            CH_CHK(ch_recurse_pack_dm(info, desc->td));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_OVERRIDE_COUNT);
            CH_CHK_PACK(int(pk, desc->override_count));
            CH_CHK_PACK_CSTR(pk, CHMPK_MSG_TD_TOL);
            CH_CHK_PACK(float(pk, desc->fieldTolerance));
        }
    }
    return 0;
}

void ch_send_datamap_cb(const datamap_t* dm, void* vinfo)
{
    ch_send_datamap_cb_info* info = vinfo;
    // ch_send_log_info(ctx, "Found datamap '%s', %d fields", dm->dataClassName, dm->dataNumFields);
    if (ch_msg_preamble(info->send_ctx, CH_MSG_DATAMAP) || ch_recurse_pack_dm(info, dm))
        ch_clean_exit(info->send_ctx, 1);
    ch_send_msgpack(info->send_ctx);
}

void ch_send_err_and_exit(ch_send_ctx* ctx, const char* fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);
    ch_send_vstrf(ctx, CH_MSG_LOG_ERROR, fmt, vargs);
    va_end(vargs);
    ch_clean_exit(ctx, 1);
}
