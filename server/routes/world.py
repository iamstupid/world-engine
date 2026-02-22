from fastapi import APIRouter, HTTPException
from fastapi.responses import Response
from models.params import FieldStatsResponse, WorldInfo, BufferInfo, BufferListResponse
from services.world_store import store
import struct
import zlib
import numpy as np

router = APIRouter()

# FILD dtype code -> numpy dtype
DTYPE_MAP = {
    0: np.float32,
    1: np.uint8,
    7: np.uint16,
}


def _parse_fild_data(packet: bytes):
    """Parse a FILD packet and return (numpy_array, dtype_code)."""
    header = packet[:48]
    dtype_code = struct.unpack_from("<I", header, 16)[0]
    compression = struct.unpack_from("<I", header, 20)[0]
    payload = packet[48:]

    np_dtype = DTYPE_MAP.get(dtype_code, np.float32)

    if compression == 0:
        data = np.frombuffer(payload, dtype=np_dtype)
    elif compression == 1:
        raw = zlib.decompress(payload)
        data = np.frombuffer(raw, dtype=np_dtype)
    else:
        raise ValueError(f"Unsupported compression: {compression}")

    return data, dtype_code


@router.get("/world/{world_id}/field/{name}")
async def get_field(world_id: str, name: str):
    entry = store.get(world_id)
    if not entry:
        raise HTTPException(404, f"World {world_id} not found")
    if name not in entry.fields:
        raise HTTPException(404, f"Field {name} not found in world {world_id}")

    packet = entry.fields[name]
    return Response(
        content=packet,
        media_type="application/octet-stream",
        headers={"Content-Disposition": f'attachment; filename="{name}.fild"'},
    )


@router.get("/world/{world_id}/field/{name}/stats", response_model=FieldStatsResponse)
async def get_field_stats(world_id: str, name: str):
    entry = store.get(world_id)
    if not entry:
        raise HTTPException(404, f"World {world_id} not found")
    if name not in entry.fields:
        raise HTTPException(404, f"Field {name} not found")

    packet = entry.fields[name]
    data, _ = _parse_fild_data(packet)

    return FieldStatsResponse(
        min=float(data.min()),
        max=float(data.max()),
        mean=float(data.mean()),
        std=float(data.std()),
    )


# --- Buffer endpoints ---

@router.get("/world/{world_id}/buffers", response_model=BufferListResponse)
async def get_buffer_list(world_id: str):
    entry = store.get(world_id)
    if not entry:
        raise HTTPException(404, f"World {world_id} not found")

    buffers = [
        BufferInfo(
            name=meta.name,
            dtype=meta.dtype,
            display_name=meta.display_name,
            colormap=meta.colormap,
        )
        for meta in entry.buffers.values()
    ]

    return BufferListResponse(world_id=world_id, buffers=buffers)


@router.get("/world/{world_id}/buffer/{name}")
async def get_buffer(world_id: str, name: str):
    entry = store.get(world_id)
    if not entry:
        raise HTTPException(404, f"World {world_id} not found")
    if name not in entry.buffers:
        raise HTTPException(404, f"Buffer {name} not found in world {world_id}")

    packet = entry.buffers[name].fild_bytes
    return Response(
        content=packet,
        media_type="application/octet-stream",
        headers={"Content-Disposition": f'attachment; filename="{name}.fild"'},
    )


@router.get("/world/{world_id}/buffer/{name}/stats", response_model=FieldStatsResponse)
async def get_buffer_stats(world_id: str, name: str):
    entry = store.get(world_id)
    if not entry:
        raise HTTPException(404, f"World {world_id} not found")
    if name not in entry.buffers:
        raise HTTPException(404, f"Buffer {name} not found")

    packet = entry.buffers[name].fild_bytes
    data, _ = _parse_fild_data(packet)

    return FieldStatsResponse(
        min=float(data.min()),
        max=float(data.max()),
        mean=float(data.mean()),
        std=float(data.std()),
    )


@router.get("/worlds")
async def list_worlds():
    entries = store.list_all()
    return [
        WorldInfo(
            world_id=e.world_id,
            N=e.N,
            mesh_type=e.mesh_type,
            fields=list(e.fields.keys()),
        )
        for e in entries
    ]


@router.delete("/world/{world_id}")
async def delete_world(world_id: str):
    if not store.delete(world_id):
        raise HTTPException(404, f"World {world_id} not found")
    return {"status": "deleted", "world_id": world_id}
