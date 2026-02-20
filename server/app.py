import sys
import os

# Add build directory to Python path so worldengine_py can be found
BUILD_LIB = os.path.join(os.path.dirname(__file__), "..", "build", "library")
sys.path.insert(0, BUILD_LIB)

# Also set LD_LIBRARY_PATH for the shared library
if "LD_LIBRARY_PATH" not in os.environ:
    os.environ["LD_LIBRARY_PATH"] = BUILD_LIB
else:
    os.environ["LD_LIBRARY_PATH"] = BUILD_LIB + ":" + os.environ["LD_LIBRARY_PATH"]

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from routes import generate, world, mesh

app = FastAPI(title="WorldEngine API", version="0.1.0")

app.include_router(generate.router, prefix="/api/v1")
app.include_router(world.router, prefix="/api/v1")
app.include_router(mesh.router, prefix="/api/v1")

# Serve client files
CLIENT_DIR = os.path.join(os.path.dirname(__file__), "..", "client")
if os.path.isdir(CLIENT_DIR):
    app.mount("/", StaticFiles(directory=CLIENT_DIR, html=True), name="client")
