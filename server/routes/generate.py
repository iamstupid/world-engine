from fastapi import APIRouter, HTTPException
from models.params import GenerateRequest, GenerateResponse, PlateGenerateRequest
from services.world_store import store, BufferMeta
from services.generator import generate_noise
from services.plate_generator import generate_plates

router = APIRouter()


@router.post("/generate/noise", response_model=GenerateResponse)
async def generate_noise_endpoint(req: GenerateRequest):
    if req.mesh_type != "ico":
        raise HTTPException(400, f"Unsupported mesh type: {req.mesh_type}")
    if req.N < 1:
        raise HTTPException(400, f"N must be >= 1, got {req.N}")

    params = {
        "seed": req.noise.seed,
        "octaves": req.noise.octaves,
        "frequency": req.noise.frequency,
        "lacunarity": req.noise.lacunarity,
        "gain": req.noise.gain,
        "warp_amplitude": req.noise.warp_amplitude,
        "ocean_fraction": req.noise.ocean_fraction,
    }

    packet, elapsed_ms = generate_noise(req.N, params)

    entry = store.create(req.mesh_type, req.N)
    entry.buffers["elevation"] = BufferMeta(
        name="elevation",
        dtype="float32",
        display_name="Elevation",
        colormap="terrain",
        fild_bytes=packet,
    )

    return GenerateResponse(
        world_id=entry.world_id,
        mesh_type=req.mesh_type,
        N=req.N,
        fields=list(entry.fields.keys()),
        elapsed_ms=round(elapsed_ms, 1),
    )


@router.post("/generate/plates")
async def generate_plates_endpoint(req: PlateGenerateRequest):
    entry = store.get(req.world_id)
    if not entry:
        raise HTTPException(404, f"World {req.world_id} not found")
    if "elevation" not in entry.buffers:
        raise HTTPException(400, "Elevation must be generated before plates")

    params = {
        "num_plates": req.plates.num_plates,
        "seed": req.plates.seed,
        "ocean_bias": req.plates.ocean_bias,
        "weight_octaves": req.plates.weight_octaves,
        "weight_frequency": req.plates.weight_frequency,
        "weight_lacunarity": req.plates.weight_lacunarity,
        "weight_gain": req.plates.weight_gain,
    }

    elevation_fild = entry.buffers["elevation"].fild_bytes
    packet, elapsed_ms = generate_plates(entry.k, elevation_fild, params)

    entry.buffers["plate_id"] = BufferMeta(
        name="plate_id",
        dtype="uint16",
        display_name="Plate ID",
        colormap="categorical",
        fild_bytes=packet,
    )

    return {
        "world_id": entry.world_id,
        "fields": list(entry.fields.keys()),
        "elapsed_ms": round(elapsed_ms, 1),
    }
