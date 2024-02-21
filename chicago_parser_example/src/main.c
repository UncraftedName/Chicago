#include <stdio.h>
#include <string.h>

#include "ch_save.h"

int main()
{
    ch_parsed_save_data save_data;
    ch_err err = ch_parse_save_from_file(&save_data, "G:/Games/portal/Portal Source/portal/SAVE/00.sav");
    if (err)
        printf("Parsing failed with error: %s", ch_err_strs[err]);
    else
        printf("Test result: %.4s", save_data.header.id);
    ch_free_save_data(&save_data);
    return 0;
}
