import time
import struct
import zlib
import numpy as np
import worldengine_py as we


def generate_plates(k: int, elevation_fild: bytes, params: dict) -> tuple[bytes, float]:
    """Assign tectonic plates using C++ Dijkstra and return serialized FILD packet + elapsed time."""
    start = time.perf_counter()

    # Extract raw elevation float array from FILD bytes
    header = elevation_fild[:48]
    compression = struct.unpack_from("<I", header, 20)[0]
    payload = elevation_fild[48:]

    if compression == 0:
        elevation_np = np.frombuffer(payload, dtype=np.float32)
    elif compression == 1:
        raw = zlib.decompress(payload)
        elevation_np = np.frombuffer(raw, dtype=np.float32)
    else:
        raise ValueError(f"Unsupported FILD compression: {compression}")

    # Call C++ plate assignment
    result = we.assign_plates(k, elevation_np, params)
    packet = we.serialize_v2_u16(result, 1)

    elapsed_ms = (time.perf_counter() - start) * 1000
    return packet, elapsed_ms
