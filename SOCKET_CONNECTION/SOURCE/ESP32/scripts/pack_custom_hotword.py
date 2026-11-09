#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Tool dong goi mo hinh ESP-SR WakeNet (.bin) dung cho chuc nang tai len Hotword tuy chinh tren ESP32-S3 VBot Client.
"""

import os
import sys
import struct
import argparse

def pack_model_dir(model_dir, output_bin, model_name=None):
    if not os.path.isdir(model_dir):
        print(f"Loi: Thu muc khong ton tai: {model_dir}")
        return False

    if not model_name:
        model_name = os.path.basename(os.path.abspath(model_dir))

    # Kiem tra ten model phai co tien to 'wn' (WakeNet)
    if not model_name.startswith("wn"):
        print(f"Canh bao: Ten mo hinh '{model_name}' nen bat dau bang 'wn' (vi du: wn9_vbot, wn9_custom).")

    # Tim cac file trong thu muc
    candidate_files = []
    for f in os.listdir(model_dir):
        fpath = os.path.join(model_dir, f)
        if os.path.isfile(fpath) and not f.startswith('.'):
            candidate_files.append((f, fpath))

    if not candidate_files:
        print(f"Loi: Khong tim thay file nao trong {model_dir}")
        return False

    # Sap xep de _MODEL_INFO_ hoac data/index co thu tu on dinh
    candidate_files.sort(key=lambda x: x[0])

    print(f"Dong goi mo hinh: '{model_name}' tu thu muc '{model_dir}'")
    print(f"Cac file thanh phan ({len(candidate_files)} file):")
    file_bytes_list = []
    total_file_bytes = 0
    for fname, fpath in candidate_files:
        with open(fpath, "rb") as fp:
            data = fp.read()
        file_bytes_list.append((fname, data))
        total_file_bytes += len(data)
        print(f"  - {fname}: {len(data)} bytes")

    # Header structure:
    # 4 bytes: model_num = 1
    # 32 bytes: model_name (null-padded)
    # 4 bytes: file_num
    # For each file:
    #   32 bytes: filename (null-padded)
    #   4 bytes: offset
    #   4 bytes: size
    file_num = len(file_bytes_list)
    header_size = 4 + 32 + 4 + file_num * (32 + 4 + 4)

    header = bytearray()
    header += struct.pack('<I', 1) # model_num = 1
    mname_bytes = model_name.encode('utf-8')[:31]
    header += mname_bytes.ljust(32, b'\x00')
    header += struct.pack('<I', file_num)

    curr_offset = header_size
    data_section = bytearray()

    for fname, fdata in file_bytes_list:
        fname_bytes = fname.encode('utf-8')[:31]
        header += fname_bytes.ljust(32, b'\x00')
        header += struct.pack('<II', curr_offset, len(fdata))
        curr_offset += len(fdata)
        data_section += fdata

    final_bin = header + data_section

    out_dir = os.path.dirname(os.path.abspath(output_bin))
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    with open(output_bin, "wb") as fp:
        fp.write(final_bin)

    print(f"\n=> Dong goi thanh cong: {output_bin}")
    print(f"   Tong dung luong: {len(final_bin)} bytes ({len(final_bin)/1024:.1f} KB)")
    print("   Ban co the truy cap WebUI ESP32-S3 va tai tep nay len muc 'Quan Ly Hotword On-Device'!")
    return True

def main():
    parser = argparse.ArgumentParser(description="Dong goi thu muc mo hinh WakeNet thanh file .bin cho ESP32-S3 VBot Client")
    parser.add_argument("-d", "--dir", required=True, help="Duong dan thu muc chua mo hinh (chua wn9_data, wn9_index, _MODEL_INFO_)")
    parser.add_argument("-o", "--output", required=True, help="Duong dan file .bin dau ra (vi du: wn9_custom.bin)")
    parser.add_argument("-n", "--name", default=None, help="Ten dinh danh model (mac dinh lay ten thu muc)")

    args = parser.parse_args()
    pack_model_dir(args.dir, args.output, args.name)

if __name__ == "__main__":
    main()
