from fastapi import APIRouter, HTTPException
from models.params import MeshInfoResponse

router = APIRouter()


@router.get("/mesh/ico/{N}", response_model=MeshInfoResponse)
async def get_mesh_info(N: int):
    if N < 1:
        raise HTTPException(400, f"N must be >= 1, got {N}")

    num_cells = 10 * N * N + 2
    num_rows = 3 * N + 1
    num_faces = 2 * num_cells - 4  # Euler formula for triangulated sphere

    return MeshInfoResponse(
        N=N,
        num_cells=num_cells,
        num_rows=num_rows,
        num_faces=num_faces,
    )
