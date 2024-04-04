#include "ch_recv.h"

typedef enum ch_unpack_result {
    // all is well
    CH_UNPACK_OK,
    // got goodbye, we can stop now
    CH_UNPACK_EXIT,
    // callee reports error
    CH_UNPACK_CONSTRAINT_FAIL,
    // caller reports error
    CH_UNPACK_OUT_OF_MEMORY,
    CH_UNPACK_WRONG_FORMAT,
} ch_unpack_result;

#define CH_CHECK(cond)                     \
    do {                                   \
        if (!(cond)) {                     \
            assert(0);                     \
            return CH_UNPACK_WRONG_FORMAT; \
        }                                  \
    } while (0)

#define CH_CHECK_STR(mp_str, comp_str) CH_CHECK(!strncmp((mp_str).ptr, comp_str, (mp_str).size))

ch_unpack_result ch_process_msg(ch_process_msg_ctx* ctx, msgpack_object o)
{
    CH_CHECK(o.type == MSGPACK_OBJECT_MAP);
    CH_CHECK(o.via.map.size == 2);
    CH_CHECK(o.via.map.ptr[0].key.type == MSGPACK_OBJECT_STR);
    CH_CHECK_STR(o.via.map.ptr[0].key.via.str, CHMPK_MSG_TYPE);
    CH_CHECK(o.via.map.ptr[0].val.type == MSGPACK_OBJECT_POSITIVE_INTEGER);
    CH_CHECK(o.via.map.ptr[1].key.type == MSGPACK_OBJECT_STR);
    CH_CHECK_STR(o.via.map.ptr[1].key.via.str, CHMPK_MSG_DATA);

    const msgpack_object_kv* kv = o.via.map.ptr;
    ch_comm_msg_type msg_type = kv[0].val.via.u64;
    if (msg_type != CH_MSG_HELLO && !ctx->got_hello) {
        CH_LOG_ERROR(ctx, "Payload sent other messages before sending HELLO.");
        return CH_UNPACK_CONSTRAINT_FAIL;
    }
    switch (msg_type) {
        case CH_MSG_HELLO:
            CH_CHECK(kv[1].val.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got HELLO message from payload.\n");
            ctx->got_hello = true;
            return CH_UNPACK_OK;
        case CH_MSG_GOODBYE:
            CH_CHECK(kv[1].val.type == MSGPACK_OBJECT_NIL);
            CH_LOG_INFO(ctx, "Got GOODBYE message from payload.\n");
            return CH_UNPACK_EXIT;
        case CH_MSG_LOG_INFO:
        case CH_MSG_LOG_ERROR:
            CH_CHECK(kv[1].val.type == MSGPACK_OBJECT_STR);
            ch_log_level level = msg_type == CH_MSG_LOG_INFO ? CH_LL_INFO : CH_LL_ERROR;
            CH_LOG_LEVEL(level, ctx, "[payload] %.*s\n", kv[1].val.via.str.size, kv[1].val.via.str.ptr);
            return CH_UNPACK_OK;
        case CH_MSG_DATAMAP:
        case CH_MSG_LINKED_NAME:
        default:
            return CH_UNPACK_WRONG_FORMAT;
    }
}

bool ch_unpack_and_process_msg(ch_process_msg_ctx* ctx, const char* buf, size_t buf_size)
{
    size_t off = 0;
    msgpack_object o;
    msgpack_unpack_return ret = msgpack_unpack(buf, buf_size, &off, &ctx->mp_zone, &o);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        CH_LOG_ERROR(ctx, "msgpack_unpack failed with return %d.", ret);
        return false;
    }

#if 0
    CH_LOG_INFO(ctx, "recv msg (%zu bytes)\n", buf_size);
    msgpack_object_print(stdout, o);
    fprintf(stdout, "\n");
#endif

    switch (ch_process_msg(ctx, o)) {
        case CH_UNPACK_OK:
            break;
        case CH_UNPACK_EXIT:
        case CH_UNPACK_CONSTRAINT_FAIL:
            return false;
        case CH_UNPACK_WRONG_FORMAT:
            CH_LOG_ERROR(ctx, "Received wrong msgpack format from payload.");
            return false;
        case CH_UNPACK_OUT_OF_MEMORY:
            CH_LOG_ERROR(ctx, "Out of memory while unpacking message from payload.");
            return false;
        default:
            assert(0);
            return false;
    }

    /*
    * Sit down, and get in the zone. No, not the vibe zone silly! The msgpack zone!!!
    * When msgpack unpacks an object, it needs to allocate memory for storing the msgpack_objects
    * that represent the unpacked structure. These bjects are stored in a msgpack_zone, and there
    * are 3 ways to unpack streams provided by msgpack:
    * 
    * 1) msgpack_unpack_next: assumes that the original buffer will remain alive while processing
    * 2) msgpack_unpacker_next: copies the original buffer into the zone
    * 3) msgpack_unpack: same as 1, but allows you to reuse a zone
    * 
    * I was told by people who know more about programming than I do that allocations are slow.
    * In my case, the buffer will remain alive while I unpack (I'm probably not going to be
    * multithreading this app), so options 1 & 3 look good. Option 1 reallocates a zone every
    * time I unpack, so I think I want option 3. For some reason, the docs say that option 3 is
    * obsolete, and also msgpack doesn't give a way to check if a zone was expanded during
    * unpacking. But whatever, I'm using option 3 and a small hack to check if the zone grew.
    */

    struct mp_chunk {
        struct mp_chunk* next;
    };

    if (((struct mp_chunk*)ctx->mp_zone.chunk_list.head)->next) {
        // free the old zone, allocate a new one twice the zone (no realloc boohoo)
        size_t old_chunk_size = ctx->mp_zone.chunk_size;
        msgpack_zone_destroy(&ctx->mp_zone);
        msgpack_zone_init(&ctx->mp_zone, old_chunk_size * 2);
    } else {
        msgpack_zone_clear(&ctx->mp_zone);
    }

    return true;
}
