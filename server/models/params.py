from pydantic import BaseModel, Field
from typing import Optional


class NoiseParamsModel(BaseModel):
    seed: int = 42
    octaves: int = 6
    frequency: float = 1.5
    lacunarity: float = 2.0
    gain: float = 0.5
    warp_amplitude: float = 0.0
    ocean_fraction: float = 0.55


class GenerateRequest(BaseModel):
    mesh_type: str = "ico"
    N: int = 100
    noise: NoiseParamsModel = NoiseParamsModel()


class GenerateResponse(BaseModel):
    world_id: str
    mesh_type: str
    N: int
    fields: list[str]
    elapsed_ms: float


class MeshInfoResponse(BaseModel):
    N: int
    num_cells: int
    num_rows: int
    num_faces: int


class FieldStatsResponse(BaseModel):
    min: float
    max: float
    mean: float
    std: float


class WorldInfo(BaseModel):
    world_id: str
    N: int
    mesh_type: str
    fields: list[str]


class PlateParamsModel(BaseModel):
    num_plates: int = 12
    seed: int = 42
    ocean_bias: float = 1.5
    weight_octaves: int = 4
    weight_frequency: float = 1.0
    weight_lacunarity: float = 2.0
    weight_gain: float = 0.5


class PlateGenerateRequest(BaseModel):
    world_id: str
    plates: PlateParamsModel = PlateParamsModel()


class BufferInfo(BaseModel):
    name: str
    dtype: str
    display_name: str
    colormap: str


class BufferListResponse(BaseModel):
    world_id: str
    buffers: list[BufferInfo]
