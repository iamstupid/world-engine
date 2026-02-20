import uuid
import time
from dataclasses import dataclass, field


@dataclass
class WorldEntry:
    world_id: str
    mesh_type: str
    N: int
    created_at: float
    fields: dict = field(default_factory=dict)  # name -> serialized bytes


class WorldStore:
    def __init__(self):
        self._worlds: dict[str, WorldEntry] = {}

    def create(self, mesh_type: str, N: int) -> WorldEntry:
        world_id = f"w_{uuid.uuid4().hex[:8]}"
        entry = WorldEntry(
            world_id=world_id,
            mesh_type=mesh_type,
            N=N,
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
