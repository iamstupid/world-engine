import uuid
import time
import math
from dataclasses import dataclass, field


@dataclass
class BufferMeta:
    name: str
    dtype: str            # "float32" or "uint16"
    display_name: str
    colormap: str         # "terrain", "categorical", etc.
    fild_bytes: bytes = b""


@dataclass
class WorldEntry:
    world_id: str
    mesh_type: str
    N: int
    k: int
    created_at: float
    buffers: dict = field(default_factory=dict)  # name -> BufferMeta

    @property
    def fields(self) -> dict:
        """Backward-compatible read-through: name -> fild_bytes."""
        return {name: meta.fild_bytes for name, meta in self.buffers.items()}


class WorldStore:
    def __init__(self):
        self._worlds: dict[str, WorldEntry] = {}

    def create(self, mesh_type: str, N: int) -> WorldEntry:
        world_id = f"w_{uuid.uuid4().hex[:8]}"
        k = int(math.log2(N)) if N > 0 else 0
        entry = WorldEntry(
            world_id=world_id,
            mesh_type=mesh_type,
            N=N,
            k=k,
            created_at=time.time(),
        )
        self._worlds[world_id] = entry
        return entry

    def get(self, world_id: str) -> WorldEntry | None:
        return self._worlds.get(world_id)

    def delete(self, world_id: str) -> bool:
        if world_id in self._worlds:
            del self._worlds[world_id]
            return True
        return False

    def list_all(self) -> list[WorldEntry]:
        return list(self._worlds.values())


# Singleton
store = WorldStore()
