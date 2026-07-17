#!/usr/bin/env python3
"""
Decrypts a Pocket Cryptid capture log (.pclog) and prints one line per record.

The AES-256 key is never stored on the SD card — read it off the device once via
Settings > Reveal log key, then pass it here as 64 hex characters.

Usage:
    python3 decrypt_log.py --key <64-hex-char key> path/to/20260717.pclog [more.pclog ...]

Record layout matches lib/CryptidLog/LogRecord.h — keep the two in sync if that ever changes,
and bump kLogFormatVersion on the firmware side so old logs stay readable by old copies of this
script.
"""

import argparse
import datetime
import struct
import sys

try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except ImportError:
    sys.exit("Missing dependency: pip install cryptography")

RECORD_TYPES = {0: "WIFI_AP", 1: "WIFI_CLIENT", 2: "WIFI_HANDSHAKE", 3: "BLE_DEVICE"}

NONCE_LEN = 12
TAG_LEN = 16
PLAINTEXT_FORMAT = "<IB6sbB24sBB"  # unixTime, type, mac[6], rssi, extra, label[24], labelLen, eapolMsgNum
PLAINTEXT_LEN = struct.calcsize(PLAINTEXT_FORMAT)


def mac_str(mac_bytes: bytes) -> str:
    return ":".join(f"{b:02x}" for b in mac_bytes)


def format_record(plaintext: bytes) -> str:
    unix_time, rec_type, mac, rssi, extra, label, label_len, eapol_msg = struct.unpack(PLAINTEXT_FORMAT, plaintext)
    type_name = RECORD_TYPES.get(rec_type, f"UNKNOWN({rec_type})")
    label_str = label[:label_len].decode("utf-8", errors="replace")

    if unix_time > 1_600_000_000:  # plausible real calendar time (device clock had been set)
        ts = datetime.datetime.utcfromtimestamp(unix_time).strftime("%Y-%m-%d %H:%M:%S UTC")
    else:
        ts = f"+{unix_time}s (clock never set — relative to boot)"

    extra_str = f"ch{extra}" if rec_type in (0, 1, 2) else ("random" if extra else "public")
    handshake_str = f" eapol_msg={eapol_msg}" if rec_type == 2 else ""
    label_part = f' "{label_str}"' if label_str else ""

    return f"[{ts}] {type_name:15s} {mac_str(mac)} rssi={rssi:>4} {extra_str}{label_part}{handshake_str}"


def decrypt_file(path: str, key: bytes):
    aesgcm = AESGCM(key)
    with open(path, "rb") as f:
        data = f.read()

    offset = 0
    count = 0
    errors = 0
    while offset < len(data):
        if offset + 1 + NONCE_LEN + 2 > len(data):
            break
        version = data[offset]
        offset += 1
        nonce = data[offset : offset + NONCE_LEN]
        offset += NONCE_LEN
        (ct_len,) = struct.unpack("<H", data[offset : offset + 2])
        offset += 2
        if offset + ct_len + TAG_LEN > len(data):
            print(f"  ! truncated record near offset {offset}, stopping", file=sys.stderr)
            break
        ciphertext = data[offset : offset + ct_len]
        offset += ct_len
        tag = data[offset : offset + TAG_LEN]
        offset += TAG_LEN

        if version != 1:
            print(f"  ! unsupported record version {version}, skipping", file=sys.stderr)
            errors += 1
            continue

        try:
            plaintext = aesgcm.decrypt(nonce, ciphertext + tag, None)
        except Exception as e:  # noqa: BLE001 - report and keep going
            print(f"  ! failed to decrypt record at offset {offset}: {e}", file=sys.stderr)
            errors += 1
            continue

        if len(plaintext) != PLAINTEXT_LEN:
            print(f"  ! unexpected plaintext length {len(plaintext)}, skipping", file=sys.stderr)
            errors += 1
            continue

        print(format_record(plaintext))
        count += 1

    print(f"-- {path}: {count} records decrypted, {errors} errors --", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--key", required=True, help="64 hex character AES-256 key from Settings > Reveal log key")
    parser.add_argument("files", nargs="+", help=".pclog files to decrypt")
    args = parser.parse_args()

    try:
        key = bytes.fromhex(args.key)
    except ValueError:
        sys.exit("--key must be 64 hex characters")
    if len(key) != 32:
        sys.exit(f"--key must decode to 32 bytes (AES-256), got {len(key)}")

    for path in args.files:
        decrypt_file(path, key)


if __name__ == "__main__":
    main()
