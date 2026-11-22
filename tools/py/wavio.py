"""Tiny WAV reader (16/24-bit PCM and 32-bit float) returning float64 arrays shaped (frames, channels)."""
import struct
import numpy as np

def read_wav(path):
    with open(path, 'rb') as f:
        data = f.read()
    assert data[:4] == b'RIFF' and data[8:12] == b'WAVE', 'not a WAV file'
    pos = 12
    fmt = None
    raw = None
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack('<I', data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b'fmt ':
            tag, nch, sr, _, _, bits = struct.unpack('<HHIIHH', body[:16])
            fmt = (tag, nch, sr, bits)
        elif cid == b'data':
            raw = body
        pos += 8 + size + (size & 1)
    tag, nch, sr, bits = fmt
    if tag == 3 and bits == 32:
        x = np.frombuffer(raw, dtype='<f4').astype(np.float64)
    elif bits == 16:
        x = np.frombuffer(raw, dtype='<i2').astype(np.float64) / 32768.0
    elif bits == 24:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        v = np.where(v & 0x800000, v - 0x1000000, v)
        x = v.astype(np.float64) / 8388608.0
    else:
        raise ValueError('unsupported WAV format')
    return x.reshape(-1, nch), sr
