from fastapi import APIRouter, HTTPException
from fastapi.responses import Response
from models.params import FieldStatsResponse, WorldInfo
from services.world_store import store
import struct
import numpy as np

router = APIRouter()


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
    # Parse FILD header to get field info
    header = packet[:48]
    num_cells = struct.unpack_from("<I", header, 12)[0]
    compression = struct.unpack_from("<I", header, 20)[0]
    payload = packet[48:]

    if compression == 0:
        data = np.frombuffer(payload, dtype=np.float32)
    elif compression == 1:
        import zlib
        raw = zlib.decompress(payload)
        data = np.frombuffer(raw, dtype=np.float32)
    else:
        raise HTTPException(500, "Unsupported compression")

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
