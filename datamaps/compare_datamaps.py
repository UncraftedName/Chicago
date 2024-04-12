import msgpack
import json
import argparse
import pathlib
from typing import Tuple, List, get_type_hints
from collections import namedtuple
import difflib
from datamap_utils import *


def dicts_equal(d1: dict, d2: dict, compare_keys: List[str]) -> List[str]:
    diff_fields = []
    for k in compare_keys:
        v1 = d1.get(k)
        v2 = d2.get(k)
        if v1 != v2:
            diff_fields.append(k)
    return diff_fields


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


class DiffChecker:
    def __init__(self, dm_sfs: List[DataMapSaveFile_V2]) -> None:
        self.dm_dfs = dm_sfs
        self.game_names = [f'{dm_sf.game_name} ({dm_sf.game_version})' for dm_sf in dm_sfs]
        self.dm_lookup = [{dm.name: dm for dm in dm_sfs[i].datamaps} for i in range(2)]
        self.dm_names = [set(self.dm_lookup[i].keys()) for i in range(2)]
        self.shared_classes = sorted(self.dm_names[0] & self.dm_names[1])

    def print_class_diffs(self):
        if self.dm_names[0] == self.dm_names[1]:
            print('Datamap classes are the same.')
            return
        added = sorted(self.dm_names[1] - self.dm_names[0])
        dropped = sorted(self.dm_names[0] - self.dm_names[1])
        if added:
            print('Added classes:')
            for name in added:
                print(f'  {name}')
            print()
        if dropped:
            print('Dropped classes:')
            for name in dropped:
                print(f'  {name}')
            print()

        # TODO check if a datamap was moved to a different module

    def print_all_dm_diffs(self):
        for sc in self.shared_classes:
            self.print_dm_diffs(sc, [self.dm_lookup[i][sc].fields for i in range(2)])

    def print_dm_diffs(self, dm_name: str, td_lists: List[List[TypeDesc_V2]]):
        diff_reported = False

        def _report_header():
            nonlocal diff_reported
            if not diff_reported:
                print(f"Class {dm_name}:")
            diff_reported = True

        def report_td_diff(s: str):
            _report_header()
            print(f'  {s}')

        def report_td_diff_list(strs: List[str]):
            _report_header()
            for s in strs:
                print(f'    {s}')

        # default pass - doesn't look at offsets, TODO make a version that does

        # drop VOID & INPUT descs, sort by offset
        # sort by offset
        td_lists = [
            sorted(
                filter(
                    lambda td: td.type not in {
                        FIELD_TYPE.VOID,
                        FIELD_TYPE.INPUT
                    } and td.input_func is None,
                    td_lists[i]
                ),
                key=lambda td: td.offset
            ) for i in range(2)
        ]
        td_lookup = [{td.name: td for td in td_lists[i]} for i in range(2)]
        td_names = [set(td_lookup[i].keys()) for i in range(2)]

        added = td_names[1] - td_names[0]
        dropped = td_names[0] - td_names[1]
        # same = td_names[0] & td_names[1]

        if added:
            report_td_diff('Added fields:')
            report_td_diff_list(sorted(added, key=lambda name: td_lookup[1][name].offset))
        if dropped:
            report_td_diff('Dropped fields:')
            report_td_diff_list(sorted(dropped, key=lambda name: td_lookup[0][name].offset))

        cmp_fields = (TD_KV_TYPE, TD_KV_FLAGS, TD_KV_TOTAL_SIZE,
                      TD_KV_INPUT_FUNC, TD_KV_RESTORE_OPS, TD_KV_EMBEDDED)
        ptr_fields = [name for name, t in get_type_hints(TypeDesc_V2).items() if t == Optional[int]]

        moved = set()
        i = 0
        j = 0
        while i < len(td_lists[0]) and j < len(td_lists[1]):
            td0 = td_lists[0][i]
            td1 = td_lists[1][j]
            if td0.name in added or td0.name in dropped or td0.name in moved:
                i += 1
                continue
            if td1.name in added or td1.name in dropped or td1.name in moved:
                j += 1
                continue
            if td0.name != td1.name:
                # field was moved
                td1 = td_lookup[1][td0.name]
                moved.add(td0.name)
                report_td_diff(f"Field '{td0.name}' was moved")
            # names are equal, compare the fields
            for cmp_field in cmp_fields:
                a0 = getattr(td0, cmp_field)
                a1 = getattr(td1, cmp_field)
                if cmp_field in ptr_fields:
                    if type(a0) != type(a1):
                        a0 = 'null' if a0 is None else 'exists'
                        a1 = 'null' if a1 is None else 'exists'
                        report_td_diff(f"Field '{td0.name}' differs by '{
                                       cmp_field}' ({a0} -> {a1})")
                else:
                    if a0 != a1:
                        report_td_diff(f"Field '{td0.name}' differs by '{
                                       cmp_field}' ({repr(a0)} -> {repr(a1)})")
            i += 1
            if td0.name not in moved:
                j += 1

        if diff_reported:
            print()


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
    # parser.add_argument(
    #     '--diff_offsets',
    #     action='store_true',
    #     help='if set, shows the differences in offsets'
    # )
    args = parser.parse_args()
    dm_paths = args.datamap_save_files
    dmos = [load_datamap_file(dm_paths[j]) for j in range(2)]
    print('Validating datamap save objects\n')
    # [validate_datamap_save_schema_v2(dmo) for dmo in dmos]
    dmos_unpacked = [save_object_to_dataclass_v2(dmo, False) for dmo in dmos]
    dc = DiffChecker(dmos_unpacked)
    print(f"Finding diffs from '{dc.game_names[0]}' to '{dc.game_names[1]}'...\n")
    dc.print_class_diffs()
    if not args.classes_only:
        dc.print_all_dm_diffs()
