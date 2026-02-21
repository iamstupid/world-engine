/* ═══════════════════════════════════════════════════════════════════════════
 *  sample_ico.hpp — Neighbor computation for an icosahedral geodesic mesh
 *
 *  Mesh layout
 *  ───────────
 *  The mesh has 3N+1 rows indexed 0 … 3N.  Row 0 is the north pole (a
 *  single cell), row 3N is the south pole.  Every other row is divided into
 *  5 identical sectors numbered 0 … 4 that wrap around the equator.
 *
 *  The number of columns per sector ("sector stride") varies by zone:
 *
 *      row 0         :  1 cell  (pole)                stride = 0*
 *      rows 1 … N-1  :  5·row  cells (top cap)       stride = row
 *      row N          :  5·N    cells (top–mid hridge) stride = N
 *      rows N+1…2N-1 :  5·N    cells (mid band)      stride = N
 *      row 2N         :  5·N    cells (bot–mid hridge) stride = N
 *      rows 2N+1…3N-1:  5·(3N-row) cells (bot cap)   stride = 3N - row
 *      row 3N        :  1 cell  (pole)                stride = 0*
 *
 *  Within each sector, a cell's position is given by col_rem ∈ [0, stride).
 *  The global column index is:  col = sec·stride + col_rem.
 *
 *  * Poles have stride=0 by convention; they are handled as special cases.
 *
 *
 *  Sector ridges and vertices
 *  ──────────────────────────
 *  Vertical ridges (VRIDGE) are cells with col_rem = 0 — they sit on the
 *  boundary between adjacent sectors.  Horizontal ridges (hridges) are rows
 *  N and 2N where the TOP or BOT cap meets the MID band.  Where a vridge
 *  crosses an hridge we get an icosahedral VERTEX (pentagon, degree 5).
 *  The two poles are also vertices.  There are exactly 12 vertices in total:
 *  2 poles + 5 at row N + 5 at row 2N.
 *
 *
 *  Triangle lean
 *  ─────────────
 *  The geodesic triangulation has two orientations.  In the top cap the
 *  sector boundaries run NE→SW, so the dominant diagonal leans SW.  In the
 *  mid band and bot cap the boundaries lean SE.  This determines which
 *  compass directions are direct neighbors (see direction patterns below).
 *
 *                    TOP CAP                    MID / BOT
 *                  · — · — ·                  · — · — ·
 *                 / \ / \ /                    \ / \ / \
 *                · — · — ·                      · — · — ·
 *
 *             vridge leans SW (\)          vridge leans SE (/)
 *
 *  At the tmid and bmid hridges both orientations meet, yielding hybrid
 *  direction patterns.  At vertices the pentagon gap removes one direction.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ─── Preprocessor machinery ─── */

#define PP_CAT(a, b)   PP_CAT_(a, b)
#define PP_CAT_(a, b)  a##b

#define PP_NARG(...)    PP_NARG_(__VA_ARGS__, PP_RSEQ())
#define PP_NARG_(...)   PP_N(__VA_ARGS__)
#define PP_N(                                                        \
    _1, _2, _3, _4, _5, _6, _7, _8,                                 \
    _9,_10,_11,_12,_13,_14,_15,_16, N, ...) N
#define PP_RSEQ()       16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0

/* SWI_k : expand k alternatives into a switch body (for neighbor_at) */
#define SWI_1( a)                   case  0: a; break;
#define SWI_2( a,b)                 case  0: a; break; case  1: b; break;
#define SWI_3( a,b,c)               case  0: a; break; case  1: b; break; case  2: c; break;
#define SWI_4( a,b,c,d)             case  0: a; break; case  1: b; break; case  2: c; break; case  3: d; break;
#define SWI_5( a,b,c,d,e)           SWI_4(a,b,c,d)           case  4: e; break;
#define SWI_6( a,b,c,d,e,f)         SWI_5(a,b,c,d,e)         case  5: f; break;

/* STI_k : emit k statements sequentially (for compute_neighbors) */
#define STI_5( a,b,c,d,e)           a;b;c;d;e;
#define STI_6( a,b,c,d,e,f)         a;b;c;d;e;f;

#define SWI(x, ...) \
    switch(x) { PP_CAT(SWI_, PP_NARG(__VA_ARGS__))(__VA_ARGS__) }
#define STITCH(...) \
    PP_CAT(STI_, PP_NARG(__VA_ARGS__))(__VA_ARGS__)


/* ═══════════════════════════════════════════════════════════════════════════
 *  Type classification
 * ═══════════════════════════════════════════════════════════════════════════ */

enum type {
    FACE      = 0,
    VERTEX    = 1,
    VRIDGE    = 2,
    TYPE_MASK = 3,
    TOP       = 4,      // row ≤ N
    BOT       = 8,      // row ≥ 2N
    MID       = 16      // N ≤ row ≤ 2N
};

/*  Zone flags encode which horizontal band(s) a cell belongs to.
 *  Since TOP, BOT, MID overlap at the boundary rows:
 *
 *      row < N        → TOP only             (zone = TOP>>2        = 1)
 *      row = N        → TOP|MID  "tmid"      (zone = (TOP|MID)>>2 = 5)
 *      N < row < 2N   → MID only             (zone = MID>>2       = 4)
 *      row = 2N       → BOT|MID  "bmid"      (zone = (BOT|MID)>>2 = 6)
 *      row > 2N       → BOT only             (zone = BOT>>2       = 2)
 *
 *  An hridge exists wherever two zone flags overlap (row N or 2N).
 *  A VERTEX is where vridge meets hridge, or at a pole.
 */
inline type get_type(int row, int col, int sec, int col_rem) {
    const int N = N_;

    const int top = (row <= N)   * TOP;
    const int bot = (row >= N*2) * BOT;
    const int mid = (row >= N &&
                     row <= N*2) * MID;
    const int hr  = ((top && mid) || (bot && mid));
    const int vr  = (col_rem == 0) * VRIDGE;
    const int vt  = ((hr && vr) || row == 0 || row == N*3) * VERTEX;
    return (type)(top | bot | mid | vr | vt);
}

int count_neighbor(int row, int col, int sec, int col_rem) {
    return 6 - (get_type(row, col, sec, col_rem) & VERTEX);
}


/* ═══════════════════════════════════════════════════════════════════════════
 *  SNEF_ — Sector-relative North / South Effective-column & width
 *  ─────────────────────────────────────────────────────────────────────────
 *  For a cell at (row, col) in sector f with col_rem c, we need five
 *  derived quantities before we can name any neighbor:
 *
 *      rw  — sector width (stride) of the current row
 *      nw  — sector width of the row above  (row − 1)
 *      sw  — sector width of the row below  (row + 1)
 *      nef — "effective column" in the north row directly above this cell
 *      sef — "effective column" in the south row directly below this cell
 *
 *  Effective-column derivation
 *  ───────────────────────────
 *  When the sector stride changes between adjacent rows, the absolute
 *  column numbering shifts.  Consider a cell at global col C in sector f.
 *  The f sectors to its left each contribute `stride` columns in the
 *  current row, but `stride ± δ` columns in the adjacent row.  So the
 *  global column of the "same position" in the adjacent row is offset by
 *  ±δ per sector to the left, i.e. ±δ·f in total.
 *
 *  Concretely, for the top cap (rows 1…N-1):
 *      stride(row)   = row          → rw = row
 *      stride(row-1) = row - 1      → nw = row - 1,  nef = col + (-1)·f
 *      stride(row+1) = row + 1      → sw = row + 1,  sef = col + (+1)·f
 *
 *  The north row is narrower: each of the f sectors to the left has one
 *  fewer column, so the "straight north" column is f positions to the left
 *  of our global col — hence nef = col − f.  The south row is wider by the
 *  symmetric argument, giving sef = col + f.
 *
 *  The same reasoning yields every zone case.  SNEF_ packages it as
 *  (base_width, north_delta, south_delta), where:
 *      nw  = base_width + nm       sw  = base_width + sm
 *      nef = col + nm·f            sef = col + sm·f
 *
 *  Zone table (nm / sm are sector-width deltas to north / south):
 *
 *      Zone    base_width   nm   sm   Reasoning
 *      ─────   ──────────   ──   ──   ─────────────────────────────────
 *      TOP     row          -1   +1   cap rows widen going south
 *      BOT     3N-row       +1   -1   cap rows narrow going south
 *      MID     N             0    0   mid-band has constant stride
 *      TMID    N            -1    0   north is top-cap (stride N-1),
 *                                     south is mid-band (stride N)
 *      BMID    N             0   -1   north is mid-band (stride N),
 *                                     south is bot-cap (stride N-1)
 * ═══════════════════════════════════════════════════════════════════════════ */

#define SNEF_(cas, row_w, nm, sm) \
        case cas: \
            rw  = row_w; \
            nw  = row_w + (nm); \
            sw  = row_w + (sm); \
            nef = col + (nm)*f; \
            sef = col + (sm)*f; \
            break;


/* ═══════════════════════════════════════════════════════════════════════════
 *  Direction macros — emit(row', col', sector, Δsector)
 *  ─────────────────────────────────────────────────────────────────────────
 *  Each macro computes the target (row, col) and a sector delta ds that
 *  tells `emit()` whether the step crosses a sector boundary.
 *
 *  Sector-crossing rules
 *  ─────────────────────
 *  The target cell's position is first computed as if it stays in sector f.
 *  If the resulting col_rem falls outside [0, target_stride), the cell
 *  actually belongs to an adjacent sector and `emit()` wraps it:
 *
 *    • Eastward overflow (E, SE, NE):  col_rem reaches target stride
 *      → step into sector f+1.  Detected by  c+1 ≥ target_width.
 *      ds = +1 when true, 0 otherwise.
 *
 *    • Westward underflow (W, SW, NW):  col_rem drops below 0
 *      → step into sector f−1.  Detected by  c < 1  (i.e. c == 0).
 *      ds = −1 when true, 0 otherwise.  The sign is negative because
 *      the sector number decreases.
 *
 *    • Pure north / south (N, S):  the target row may have a *smaller*
 *      stride than the current row.  If c ≥ target_width, the cell
 *      overflows the target sector and lands in f+1.  This only happens
 *      at boundary rows (tmid / bmid) where the adjacent zone changes.
 *      A guard `target_width > 0` prevents false triggering at poles,
 *      where the stride is 0 and the pole is handled separately.
 * ═══════════════════════════════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════════════════════════════
 *  Direction patterns  (CW order starting from E)
 *  ─────────────────────────────────────────────────────────────────────────
 *  The six compass directions are E, SE, S, SW, W, NW, N, NE.  Not all
 *  six are direct neighbors — which ones are depends on the local triangle
 *  orientation (lean).
 *
 *  For a hexagonal cell the six CW neighbors form one of two families:
 *
 *      SW-lean (top cap):  E  SE  S   W  NW  N     — diagonal runs \ (NE–SW)
 *      SE-lean (mid/bot):  E  S   SW  W  N   NE    — diagonal runs / (NW–SE)
 *
 *  On a vridge the cell straddles two sectors and the lean is modified:
 *
 *      top vridge:         E  SE  S   SW  W  N     — SW replaces NW
 *      bot vridge:         E  S   W   NW  N  NE    — NW replaces SW
 *
 *  At the tmid hridge, the north side leans SW and the south side leans SE.
 *  The hybrid CW order merges both leans:
 *
 *      tmid ridge:         E  S   SW  W  NW  N     — SE→S, NE→N absorbed
 *      tmid vertex (5):    E  S   SW  W  N         — pentagon gap at NW/NE
 *
 *  At the bmid hridge, north leans SE and south leans SW:
 *
 *      bmid non-ridge:     E  S   SW  W  N   NE    — same as mid/bot
 *      bmid vertex (5):    E  S   W   N   NE       — pentagon gap at SW/NW
 *
 *  The typ>>1 encoding maps to these patterns:
 *
 *      half=2      TOP interior         E SE S  W  NW N
 *      half=3      TOP vridge           E SE S  SW W  N
 *      half=4,8,   BOT/MID interior     E S  SW W  N  NE
 *        9,12      MID vridge, BMID
 *      half=5      BOT vridge           E S  W  NW N  NE
 *      half=10     TMID ridge           E S  SW W  NW N
 *      half=11     TMID vertex          E S  SW W  N       (5 neighbors)
 *      half=13     BMID vertex          E S  W  N  NE      (5 neighbors)
 * ═══════════════════════════════════════════════════════════════════════════ */


/* ─── neighbor_at : single-direction lookup ─── */

NeighborRC neighbor_at(int row, int col, int sec, int col_rem, int dir) const {
    const int N = N_;
    const int f = sec;
    const int c = col_rem;

    /* North pole: sectors 0-4 wind CCW when viewed from above, so
       CW order is 0, 4, 3, 2, 1 — i.e. sector = (5 - dir) % 5. */
    if (row == 0) {
        int s = (5 - dir) % 5;
        return emit(1, s, s, 0);
    }
    if (row == 3 * N)
        return emit(3*N - 1, dir, dir, 0);

    const type typ = get_type(row, col, sec, col_rem);

#define em_(...) return emit(__VA_ARGS__)
#define E  em_(row,   col+1,  f,  (c+1>=rw))
#define SE em_(row+1, sef+1,  f,  (c+1>=sw))
#define S  em_(row+1, sef,    f,  (sw > 0 && c >= sw))
#define SW em_(row+1, sef-1,  f, -(c<1))
#define W  em_(row,   col-1,  f, -(c<1))
#define NW em_(row-1, nef-1,  f, -(c<1))
#define N  em_(row-1, nef,    f,  (nw > 0 && c >= nw))
#define NE em_(row-1, nef+1,  f,  (c+1>=nw))

    int rw, nw, sw, sef, nef;
    switch (typ >> 2) {
        SNEF_(1,  row,       (-1),  1)      // TOP
        SNEF_(2,  3*N - row,  1,   (-1))    // BOT
        SNEF_(4,  N,          0,    0)       // MID
        SNEF_(5,  N,         (-1),  0)       // TMID
        SNEF_(6,  N,          0,   (-1))     // BMID
    }

    switch (typ >> 1) {
        case 2:
            SWI(dir, E, SE, S, W, NW, N)
            break;
        case 3:
            SWI(dir, E, SE, S, SW, W, N)
            break;
        case 4: case 8: case 9: case 12:
            SWI(dir, E, S, SW, W, N, NE)
            break;
        case 5:
            SWI(dir, E, S, W, NW, N, NE)
            break;
        case 10:
            SWI(dir, E, S, SW, W, NW, N)
            break;
        case 11:
            SWI(dir, E, S, SW, W, N)
            break;
        case 13:
            SWI(dir, E, S, W, N, NE)
            break;
    }
#undef em_
#undef E
#undef SE
#undef S
#undef SW
#undef W
#undef NW
#undef N
#undef NE
    __builtin_unreachable();
}


/* ─── compute_neighbors : all neighbors in CW order ─── */

array<NeighborRC, 6> compute_neighbors(int row, int col, int sec,
                                        int col_rem, int& count) const {
    const int N = N_;
    const int f = sec;
    const int c = col_rem;
    array<NeighborRC, 6> ret;
    count = 0;

#define em_(...) (ret[count++] = emit(__VA_ARGS__))

    if (row == 0) {
        for (int d = 5; d >= 1; --d) em_(1, d % 5, d % 5, 0);
        return ret;
    }
    if (row == 3 * N) {
        for (int d = 0; d < 5; ++d) em_(3*N - 1, d, d, 0);
        return ret;
    }

    const type typ = get_type(row, col, sec, col_rem);

#define E  em_(row,   col+1,  f,  (c+1>=rw))
#define SE em_(row+1, sef+1,  f,  (c+1>=sw))
#define S  em_(row+1, sef,    f,  (sw > 0 && c >= sw))
#define SW em_(row+1, sef-1,  f, -(c<1))
#define W  em_(row,   col-1,  f, -(c<1))
#define NW em_(row-1, nef-1,  f, -(c<1))
#define N  em_(row-1, nef,    f,  (nw > 0 && c >= nw))
#define NE em_(row-1, nef+1,  f,  (c+1>=nw))

    int rw, nw, sw, sef, nef;
    switch (typ >> 2) {
        SNEF_(1,  row,       (-1),  1)
        SNEF_(2,  3*N - row,  1,   (-1))
        SNEF_(4,  N,          0,    0)
        SNEF_(5,  N,         (-1),  0)
        SNEF_(6,  N,          0,   (-1))
    }

    switch (typ >> 1) {
        case 2:
            STITCH(E, SE, S, W, NW, N)
            return ret;
        case 3:
            STITCH(E, SE, S, SW, W, N)
            return ret;
        case 4: case 8: case 9: case 12:
            STITCH(E, S, SW, W, N, NE)
            return ret;
        case 5:
            STITCH(E, S, W, NW, N, NE)
            return ret;
        case 10:
            STITCH(E, S, SW, W, NW, N)
            return ret;
        case 11:
            STITCH(E, S, SW, W, N)
            return ret;
        case 13:
            STITCH(E, S, W, N, NE)
            return ret;
    }
#undef em_
#undef E
#undef SE
#undef S
#undef SW
#undef W
#undef NW
#undef N
#undef NE
#undef SNEF_
    __builtin_unreachable();
}