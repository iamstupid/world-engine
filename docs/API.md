# Terrain Service API (Draft)

## Endpoints

- `POST /sessions`
- `GET /sessions/{id}`
- `PUT /sessions/{id}/params`
- `POST /sessions/{id}/build`
- `GET /sessions/{id}/layers`
- `GET /sessions/{id}/tiles/{layer}/{z}/{x}/{y}.png`
- `GET /sessions/{id}/images/{layer}.png`
- `GET /sessions/{id}/rivers.geojson`

## Notes

- Backend implementation is staged; current verified implementation is the C++ core and CLI.
- This contract is kept stable for the planned FastAPI adapter.
