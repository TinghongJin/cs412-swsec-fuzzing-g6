#!/usr/bin/env python3
import random
import struct
import zlib
import os

# ── PNG primitives ────────────────────────────────────────────────────────────

def make_chunk(name, data):
    crc = zlib.crc32(name + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + name + data + struct.pack(">I", crc)

def corrupt_chunk(name, data):
    bad_crc = random.randint(0, 0xFFFFFFFF)
    return struct.pack(">I", len(data)) + name + data + struct.pack(">I", bad_crc)

PNG_SIG = b'\x89PNG\r\n\x1a\n'

def make_ihdr(width, height, bit_depth=8, color_type=3, interlace=0):
    data = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, interlace)
    return make_chunk(b'IHDR', data)

def make_plte(entries):
    data = b''.join(struct.pack("BBB", r, g, b) for r, g, b in entries)
    return make_chunk(b'PLTE', data)

def make_raw_plte_chunk(raw_bytes):
    crc = zlib.crc32(b'PLTE' + raw_bytes) & 0xFFFFFFFF
    return struct.pack(">I", len(raw_bytes)) + b'PLTE' + raw_bytes + struct.pack(">I", crc)

def make_trns_palette(alphas):
    return make_chunk(b'tRNS', bytes(alphas))

def make_idat(width, height, pixel_fn):
    raw = b''
    for y in range(height):
        raw += b'\x00'
        raw += bytes(pixel_fn(x, y) for x in range(width))
    return make_chunk(b'IDAT', zlib.compress(raw))

def make_iend():
    return make_chunk(b'IEND', b'')

def plte_valid(n=256):
    return [(random.randint(0, 255),) * 3 for _ in range(n)]

def build_png(strategy):
    width  = random.choice([1, 2, 4, 8, 16, 32])
    height = random.choice([1, 2, 4, 8, 16, 32])
    chunks = [PNG_SIG, make_ihdr(width, height, bit_depth=8, color_type=3)]

    if strategy == "valid":
        n = random.randint(1, 256)
        chunks.append(make_plte(plte_valid(n)))

    elif strategy == "empty_plte":
        chunks.append(make_raw_plte_chunk(b''))

    elif strategy == "oversize_plte":
        data = b''.join(struct.pack("BBB", i % 256, 0, 0) for i in range(257))
        chunks.append(make_raw_plte_chunk(data))

    elif strategy == "bad_length_plte":
        n = random.choice([1, 2, 4, 5, 7, 100, 255])
        chunks.append(make_raw_plte_chunk(bytes(random.randint(0, 255) for _ in range(n))))

    elif strategy == "corrupt_crc_plte":
        data = b''.join(struct.pack("BBB", r, g, b) for r, g, b in plte_valid(random.randint(1, 256)))
        chunks.append(corrupt_chunk(b'PLTE', data))

    elif strategy == "duplicate_plte":
        chunks.append(make_plte(plte_valid(4)))
        chunks.append(make_plte(plte_valid(4)))

    elif strategy == "plte_after_idat":
        n = random.randint(1, 256)
        chunks.append(make_idat(width, height, lambda x, y: random.randint(0, n - 1)))
        chunks.append(make_plte(plte_valid(n)))
        chunks.append(make_iend())
        return b''.join(chunks)

    elif strategy == "trns_exceeds_plte":
        n_plte = random.randint(1, 10)
        chunks.append(make_plte(plte_valid(n_plte)))
        chunks.append(make_trns_palette([random.randint(0, 255) for _ in range(256)]))

    elif strategy == "index_out_of_plte":
        n_plte = random.randint(1, 4)
        chunks.append(make_plte(plte_valid(n_plte)))
        chunks.append(make_idat(width, height, lambda x, y: random.randint(0, 255)))
        chunks.append(make_iend())
        return b''.join(chunks)

    elif strategy == "missing_plte":
        pass

    elif strategy == "plte_then_corrupt_idat":
        n = random.randint(1, 256)
        chunks.append(make_plte(plte_valid(n)))
        chunks.append(make_chunk(b'IDAT', os.urandom(random.randint(1, 512))))
        chunks.append(make_iend())
        return b''.join(chunks)

    chunks.append(make_idat(width, height, lambda x, y: random.randint(0, 255)))
    chunks.append(make_iend())
    return b''.join(chunks)


STRATEGIES = [
    "valid", "empty_plte", "oversize_plte", "bad_length_plte",
    "corrupt_crc_plte", "duplicate_plte", "plte_after_idat",
    "trns_exceeds_plte", "index_out_of_plte", "missing_plte",
    "plte_then_corrupt_idat",
]

WEIGHTS = [1, 3, 3, 4, 2, 3, 3, 4, 4, 3, 2]

# ── AFL++ API — must match the official example exactly ──────────────────────

def init(seed):
    random.seed(seed)

def deinit():
    pass

def fuzz(buf, add_buf, max_size):
    """
    @type buf: bytearray
    @type add_buf: bytearray
    @type max_size: int
    @rtype: bytearray
    """
    strategy = random.choices(STRATEGIES, weights=WEIGHTS, k=1)[0]
    result = build_png(strategy)

    # occasional splice
    if add_buf and len(add_buf) > 0 and random.random() < 0.1:
        pos = random.randint(8, max(9, len(result) - 1))
        result = result[:pos] + bytes(add_buf[:max(1, max_size - pos)])

    if not result:
        return bytearray(buf)

    # must return bytearray, not bytes
    return bytearray(result[:max_size])