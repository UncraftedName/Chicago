#include <stdio.h>
#include <string.h>

#include "ch_recv.h"
#include "ch_msgpack.h"
#include "ch_save.h"

int main(void)
{
    /*ch_parsed_save_data save_data;
    ch_err err = ch_parse_save_path(&save_data, "G:/Games/portal/Portal Source/portal/SAVE/00.sav");
    if (err)
        printf("Parsing failed with error: %s\n", ch_err_strs[err]);
    else
        printf("Test result: %.4s\n", save_data.tag.id);
    ch_free_parsed_save_data(&save_data);*/

    ch_do_inject_and_recv_maps("datamaps2.msgpack", CH_LL_INFO);

    /*msgpack_sbuffer mp_buf;
    msgpack_packer mp_pk;
    msgpack_sbuffer_init(&mp_buf);
    msgpack_packer_init(&mp_pk, &mp_buf, msgpack_sbuffer_write);

    msgpack_pack_map(&mp_pk, 2);
    msgpack_pack_int(&mp_pk, 1);
    msgpack_pack_int(&mp_pk, 2);
    msgpack_pack_nil(&mp_pk);
    // msgpack_pack_int(&mp_pk, 3);
    char e[5] = {5, 6, 't', 3};
    msgpack_pack_v4raw_body(&mp_pk, e, 4);
    // msgpack_pack_int(&mp_pk, 4);

    msgpack_zone mempool;
    msgpack_zone_init(&mempool, 2048);

    msgpack_object deserialized;
    msgpack_unpack(mp_buf.data, mp_buf.size, NULL, &mempool, &deserialized);

    msgpack_object_print(stdout, deserialized);*/

    return 0;
}
