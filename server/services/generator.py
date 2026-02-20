import time
import worldengine_py as we


def generate_noise(N: int, params: dict) -> tuple[bytes, float]:
    """Generate noise field on an IcoMesh and return serialized FILD packet + elapsed time."""
    start = time.perf_counter()

    field = we.generate_noise_ico(N, params)
    packet = we.serialize_field(field, N, 1)  # gzip compression

    elapsed_ms = (time.perf_counter() - start) * 1000
    return packet, elapsed_ms
