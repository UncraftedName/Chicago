#include <stdarg.h>
#include "ch_dump.h"
#include "ch_save_internal.h"

ch_err ch_dump_text_printf(ch_dump_text* dump, const char* fmt, ...)
{
    // This used to write to a stack alloced buffer, but some game strings
    // are written to here which can be quite large, so I changed this to
    // use two separate passes and allocate a larger buffer if necessary.
    va_list va;
    va_start(va, fmt);
    int len = vsnprintf(NULL, 0, fmt, va);
    va_end(va);

    if ((size_t)++len > dump->write_buf_size) {
        for (dump->write_buf_size = max(256, dump->write_buf_size * 2); (size_t)len > dump->write_buf_size;
             dump->write_buf_size *= 2) {}
        dump->write_buf = ch_arena_alloc(dump->arena, dump->write_buf_size);
    }

    va_start(va, fmt);
    len = vsnprintf(dump->write_buf, dump->write_buf_size, fmt, va);
    va_end(va);

    assert(len >= 0);
    if (len < 0)
        sprintf(dump->write_buf, "DUMP ENCODING FAILED");

    for (char *cur = dump->write_buf, *end = dump->write_buf + len; cur < end;) {
        CH_RET_IF_ERR(ch_dump_text_flush_nl(dump));
        char* write_until = (char*)memchr(cur, '\n', (size_t)end - (size_t)cur);
        if (!write_until)
            write_until = end;
        else
            dump->pending_nl = true;
        fwrite(cur, 1, (size_t)write_until - (size_t)cur, dump->f);
        cur = write_until + 1;
    }
#if 0
    fflush(dump->f);
#endif
    return ferror(dump->f) ? CH_ERR_FILE_IO : CH_ERR_NONE;
}

ch_err ch_dump_text_flush_nl(ch_dump_text* dump)
{
    if (dump->pending_nl) {
        assert(dump->indent_str_buf);
        assert(dump->indent_lvl < CH_DUMP_TEXT_MAX_INDENT);
        fprintf(dump->f, "\n%.*s", dump->indent_str_len * dump->indent_lvl, dump->indent_str_buf);
        dump->pending_nl = false;
    }
    return ferror(dump->f) ? CH_ERR_FILE_IO : CH_ERR_NONE;
}

ch_err ch_dump_text_log_err(ch_dump_text* dump, const char* fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    ch_err ret = ch_append_str_ll_vfmt(dump->arena, &dump->first_error, &dump->last_error, fmt, va);
    va_end(va);
    return ret;
}

static ch_err ch_dump_default_text(ch_dump_text* dump, const char* dump_fns_name)
{
    CH_DUMP_TEXT_LOG_ERR(dump, "text dump not implemented for '%s'", dump_fns_name);
    return ch_dump_text_printf(dump, "DUMP NOT IMPLEMENTED (%s)\n", dump_fns_name);
}

static ch_err ch_dump_default_msgpack(ch_dump_msgpack* dump, const char* dump_fns_name)
{
    char buf[64];
    snprintf(buf, sizeof buf, "DUMP NOT IMPLEMENTED (%s)", dump_fns_name);
    buf[sizeof(buf) - 1] = '\0';
    CH_DUMP_MP_STR_CHECKED(dump, buf);
    return CH_ERR_NONE;
}

const ch_dump_default_fns g_dump_default_fns = {
    .text = ch_dump_default_text,
    .msgpack = ch_dump_default_msgpack,
};
