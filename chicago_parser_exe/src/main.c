#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "ch_recv.h"
#include "ch_save.h"
#include "ch_archive.h"

int main(void)
{
    ch_datamap_collection_info collection_save_info = {
        .output_file_path = "datamaps.ch",
        .game_name = "Portal 1",
        .game_version = "5135",
        .output_type = CH_DC_STRUCT_NAKED,
    };

    ch_do_inject_and_recv_maps(&collection_save_info, CH_LL_INFO);

    ch_byte_array ba_col;
    ch_archive_result res = ch_load_file("datamaps.ch", &ba_col, CH_COLLECTION_FILE_MAX_SIZE);
    assert(res == CH_ARCH_OK);
    res = ch_verify_and_fixup_collection_pointers(ba_col);
    assert(res == CH_ARCH_OK);
    ch_datamap_collection* col = (ch_datamap_collection*)ba_col.arr;

    ch_parsed_save_data save_data;
    ch_byte_array ba_save;
    ch_load_file("G:/Games/portal/Portal Source/portal/SAVE/00.sav", &ba_save, CH_SAVE_FILE_MAX_SIZE);
    ch_parse_info info = {
        .datamap_collection = col,
        .bytes = ba_save.arr,
        .n_bytes = ba_save.len,
    };
    ch_err err = ch_parse_save_bytes(&save_data, &info);
    if (err)
        printf("Parsing failed with error: %s\n", ch_err_strs[err]);
    else
        printf("Test result: %.4s\n", save_data.tag.id);
    ch_free_parsed_save_data(&save_data);

    return 0;
}
