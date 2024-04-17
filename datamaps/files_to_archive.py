import tarfile
import brotli
import io
import argparse
import os
import os.path
from typing import List


def write_all_to_archive(out_path: str, in_paths: List[str]):
    with io.BytesIO() as tar_iob:
        print('Creating in-memory tar file...')
        with tarfile.TarFile(fileobj=tar_iob, mode='w', dereference=True) as tf:
            for in_path in in_paths:
                print(f" - Adding '{in_path}'")
                with open(in_path, mode='rb') as inpf:
                    info = tarfile.TarInfo(os.path.basename(in_path))
                    inpf.seek(0, io.SEEK_END)
                    info.size = inpf.tell()
                    info.type = tarfile.REGTYPE
                    info.mtime = os.stat(in_path).st_mtime
                    inpf.seek(0, io.SEEK_SET)
                    tf.addfile(tarinfo=info, fileobj=inpf)
        tar_iob.seek(0, io.SEEK_SET)
        print('Compressing...')
        compressed = brotli.compress(
            string=tar_iob.read(),
            mode=brotli.MODE_GENERIC,
            quality=11,
            lgwin=22,
        )
        print(f"Writing to '{out_path}'...")
        with open(out_path, 'wb') as f:
            f.write(compressed)
        print('Done.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        'Archive Creator',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        'files',
        nargs='+',
        help='files to compress to archive',
    )
    parser.add_argument(
        '--output_file',
        default='datamap_collections.tar.br',
        help='the name of the output file',
    )
    args = parser.parse_args()
    write_all_to_archive(args.output_file, args.files)
