import msgpack
import json
import argparse
from pathlib import Path


def convert_mp_to_json(file_path):
    print(f"Loading '{file_path}'.")
    with open(file_path, "rb") as f:
        m = msgpack.load(f)
    target_file = Path(file_path).with_suffix('.json')
    print(f"Writing '{target_file}'.")
    with open(target_file, "wt") as f:
        json.dump(m, f, indent=4)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        prog='msgpack_to_json',
        description='Converts .msgpack files into .json files.'
    )
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()
    for file_path in args.files:
        convert_mp_to_json(file_path)
    print('Done.')
