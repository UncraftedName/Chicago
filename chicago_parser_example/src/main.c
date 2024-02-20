#include <stdio.h>
#include <string.h>

#include "ch_save.h"

int main()
{
    ch_parsed_save_data save_data = {0};
    ch_parse_save_from_file(&save_data, "G:/Games/portal/Portal Source/portal/SAVE/00.sav");
    printf("Test result: %s", save_data.header.id);
    ch_free_save_data(&save_data);
    return 0;
}
