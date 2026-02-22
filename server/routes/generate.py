from fastapi import APIRouter, HTTPException
from models.params import GenerateRequest, GenerateResponse
from services.world_store import store
from services.generator import generate_noise

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
    entry.fields["elevation"] = packet

    return GenerateResponse(
        world_id=entry.world_id,
        mesh_type=req.mesh_type,
        N=req.N,
        fields=list(entry.fields.keys()),
        elapsed_ms=round(elapsed_ms, 1),
    )
