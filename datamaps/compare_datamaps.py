import msgpack
import json
import argparse
import pathlib
from typing import Tuple

HEADER_CH_VERSION = 1
HEADER_KV_CH_VERSION = 'chicago_version'
HEADER_KV_GAME_NAME = 'game_name'
HEADER_KV_GAME_VERSION = 'game_version'
HEADER_KV_DATAMAPS = 'datamaps'

DM_KV_NAME = 'name'
DM_KV_OFF = 'module_offset'
DM_KV_FIELDS = 'fields'

TD_KV_NAME = 'name'
TD_KV_OFF = 'offset'
TD_KV_RESTORE_OPS = 'restore_ops'
TD_KV_INPUT_FUNC = 'input_func'


class IgnoreKeysDict:
    def __init__(self, d: dict, ignore_fields: set) -> None:
        self.d = d
        self.ignore_fields = ignore_fields

    def __eq__(self, o: object) -> bool:
        if not isinstance(o, IgnoreKeysDict):
            return False
        for k, v in self.d.items():
            if k in self.ignore_fields:
                continue
            if o.d.get(k) != v:
                return False
        return True

    # def __getattr__(self, name: str):
    #     return getattr(self.d, name)


def check_datamaps_ext(file_path: str) -> pathlib.Path:
    p = pathlib.Path(file_path)
    if p.suffix not in {'.json', '.msgpack'}:
        argparse.ArgumentTypeError('File format is not json or msgpack.')
    return p


def load_datamap_file(p: pathlib.Path) -> object:
    if p.suffix == '.json':
        with open(p, 'rt') as f:
            return json.load(f)
    else:
        with open(p, 'rb') as f:
            return msgpack.load(f)


# TODO verify the usual kv stuff
def verify_datamaps(dm):
    if not isinstance(dm, dict) \
            or dm.get(HEADER_KV_CH_VERSION) != HEADER_CH_VERSION \
            or set(dm.keys()) != {
                HEADER_KV_CH_VERSION,
                HEADER_KV_GAME_NAME,
                HEADER_KV_GAME_VERSION,
                HEADER_KV_DATAMAPS
    }:
        raise Exception('Bad file header')


def compare_datamaps(dmos: Tuple[dict, dict], classes_only: bool, ignore_offsets: bool):
    game_names = [f'{dmo[HEADER_KV_GAME_NAME]} ({dmo[HEADER_KV_GAME_VERSION]})' for dmo in dmos]
    if game_names[0] == game_names[1]:
        print(f"Game names are '{
            game_names[0]}' in both files, either they are identical or the game/version was set incorrectly.")
        return
    dm_lookup = [
        {dm[DM_KV_NAME]: dm for dm in dms[HEADER_KV_DATAMAPS]}
        for dms in dmos
    ]
    names = [set(x.keys()) for x in dm_lookup]

    # show differences in datamap classes

    if names[0] == names[1]:
        print('Datamap classes are the same.')
    else:
        for i in range(2):
            extra_names = names[i] - names[1 - i]
            if extra_names:
                print(f"Extra datamaps in '{game_names[i]}':")
                for e in extra_names:
                    print(f'  + {e}')
            else:
                print(f"No extra datamaps in '{game_names[i]}'.")

    if classes_only:
        return

    print()
    print('-'*20)
    print()

    # now compare fields

    overlap_classes = names[0] & names[1]

    td_ignore_fields = {TD_KV_RESTORE_OPS, TD_KV_INPUT_FUNC}
    if ignore_offsets:
        td_ignore_fields.add(TD_KV_OFF)

    for oc in overlap_classes:
        dms = [IgnoreKeysDict(dm_lookup[i][oc], {DM_KV_OFF}) for i in range(2)]
        for dm in dms:
            dm.d[DM_KV_FIELDS] = IgnoreKeysDict(dm.d[DM_KV_FIELDS], td_ignore_fields)
        dm_name = dms[0].d[DM_KV_NAME]
        td_lookup = [{td[TD_KV_NAME]: td for td in dm.d[DM_KV_FIELDS].d} for dm in dms]
        # I don't think anyone will care if the order of the fields in a map
        # changed, so sort them and iterate in that order.
        tds = [
            sorted(
                dm.d[DM_KV_FIELDS].d,
                key=lambda f: (f[TD_KV_OFF], f[TD_KV_NAME])
            )
            for dm in dms
        ]
        add_list = ([], [])
        diff_list = []
        i = 0
        j = 0
        while i < len(tds[0]) and j < len(tds[1]):
            if i == len(tds[0]):
                # past the end of the first type description list
                add_list[1].append(f'  + {dm_name}.{tds[1][j][TD_KV_NAME]}')
                j += 1
                continue
            if j == len(tds[0]):
                # past the end of the second type description list
                add_list[0].append(f'  + {dm_name}.{tds[0][i][TD_KV_NAME]}')
                i += 1
                continue
            td0 = IgnoreKeysDict(tds[0][i], td_ignore_fields)
            td1 = IgnoreKeysDict(tds[1][j], td_ignore_fields)
            names = (td0.d[TD_KV_NAME], td1.d[TD_KV_NAME])
            if names[0] == names[1]:
                if td0 != td1:
                    # same name, different properties
                    diff_list.append(f'field {dm_name}.{names[0]} differs')
                i += 1
                j += 1
                continue
            # one of the classes has a field that the other doesn't
            if names[0] not in td_lookup[1]:
                add_list[0].append(f'  + {dm_name}.{names[0]}')
                i += 1
            else:
                add_list[1].append(f'  + {dm_name}.{names[1]}')
                j += 1
        del i, j
        if add_list[0] or add_list[1] or diff_list:
            # we found differences!
            for diff in diff_list:
                print(diff)
            for i in range(2):
                if add_list[i]:
                    print(f"Extra fields in '{game_names[i]}' ({dm_name}):")
                    for add in add_list[i]:
                        print(add)


# TODO: the one thing this strategy doesn't account for is fields being moved around ://


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='Datamap Comparer'
    )
    parser.add_argument(
        'datamap_save_files',
        type=check_datamaps_ext,
        nargs=2,
        help='the datamaps to compare',
    )
    parser.add_argument(
        '--classes_only',
        action='store_true',
        help='if set, only show the differences in datamap classes and ignore fields'
    )
    parser.add_argument(
        '--ignore_offsets',
        action='store_true',
        help='if set, does not show differences in field offsets (only relevant if comparing fields)'
    )
    args = parser.parse_args()
    dm_paths = args.datamap_save_files
    dmos = [load_datamap_file(dm_paths[j]) for j in range(2)]
    [verify_datamaps(dmo) for dmo in dmos]
    compare_datamaps(tuple(dmos), args.classes_only, args.ignore_offsets)
