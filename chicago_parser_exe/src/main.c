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

    return 0;
}
