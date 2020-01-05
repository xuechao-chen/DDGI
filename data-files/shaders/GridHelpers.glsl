/*
    Helper functions converting texture coordinates to grid coordinates.
    Used to compute WS position of probes.
*/

#ifndef IrradianceField_glsl
#define IrradianceField_glsl

#include <g3dmath.glsl>
#include <Texture/Texture.glsl>
#include <octahedral.glsl>

#define HIGH_RES_OCT_SPACE_MARCH    10
#define HIGH_RES_OCT_SPACE_DDA      11
#define HIGH_RES_TRACE_MODE         HIGH_RES_OCT_SPACE_DDA 

// Keep in sync with LightFieldModel::TraceMode
#define WORLD_SPACE_MARCH           0
#define OCT_SPACE_HIGH_RES_ONLY     1
#define OCT_SPACE_MULTIRES          2

//#expect TRACE_MODE "LightFieldModel::TraceMode"

// for debug output
int counter = 0;

//////////////////////////////////////
#define INDEPENDENT_TRACE_8_PROBE 0 // The initial naive hack

#define INDEPENDENT_TRACE_8_PROBE_AUTO_GENERATED 1 // Auto generating the 8 probe indices, but no handoff

#define HANDOFF_TRACE_8_PROBE     2 // Handoff as appropriate to the original 8 probes

#define HANDOFF_TRACE_ALL_PROBES  3 // The "true" multi-probe algorithm, which shifts 
                                    // the 8 probes to check as proceeding through the ray

#define MULTI_PROBE_STRATEGY HANDOFF_TRACE_ALL_PROBES
//////////////////////////////////////

// The "thickness" of a surface depends on how much we trust the probe to represent visibility from
// the ray's viewpoint.  We model this as:
//
// p = probe direction to point
// v = ray direction
// n = surface normal at point
//                                                                   If the probe's viewpoint is close to the ray's,
//                                                                 we're extruding away from the viewer, so thick is safe.   
//                                                            Otherwise, we're extruding parallel to the ray, making objects
//                                                               artificially thick in screen space, which is not safe.
//                                                                                          |
//                                                                                          v
// thickness = minThickness + (maxThickness - minThickness) * (1 - abs(dot(p, n)) * max(dot(p, v), 0)
//                                                                      ^
//                                                                      |
//                                                        If the probe sees the surface at a glancing angle, then the
//                                                       surface potentially has a large depth extent in the pixel, 
//                                                        so we can treat it as very thick.
//
// We currently model only the dot(p, v) term.

const float minThickness = 0.03; // meters
const float maxThickness = 0.50; // meters

// Points exactly on the boundary in octahedral space (x = 0 and y = 0 planes) map to two different
// locations in octahedral space. We shorten the segments slightly to give unambigous locations that lead
// to intervals that lie within an octant.
const float rayBumpEpsilon    = 0.001; // meters

// If we go all the way around a cell and don't move farther than this (in m)
// then we quit the trace
const float minProgressDistance = 0.01;

//  zyx bit pattern indicating which probe we're currently using within the cell on [0, 7]
#define CycleIndex int

// On [0, L.probeCounts.x * L.probeCounts.y * L.probeCounts.z - 1]
#define ProbeIndex int

// probe xyz indices
#define GridCoord ivec3


///////////////////////////////////////////

#define TraceResult int
#define TRACE_RESULT_MISS    0
#define TRACE_RESULT_HIT     1
#define TRACE_RESULT_UNKNOWN 2

///////////////////////////////////////////

struct IrradianceField {
    Vector3int32            probeCounts;
    Point3                  probeStartPosition;
    Vector3                 probeStep;
    int                     lowResolutionDownsampleFactor;
    sampler2D               irradianceProbeGridbuffer;
    sampler2D               meanMeanSquaredProbeGridbuffer;

    int                     irradianceTextureWidth;
    int                     irradianceTextureHeight;
    int                     depthTextureWidth;
    int                     depthTextureHeight;
    int                     irradianceProbeSideLength;
    int                     depthProbeSideLength;

    float                   irradianceDistanceBias;
    float                   irradianceVarianceBias;
    float                   irradianceChebyshevBias;
    float                   normalBias;
};


float distanceSquared(Point2 v0, Point2 v1) {
    Point2 d = v1 - v0;
    return dot(d, d);
}

/** 
 \param probeCoords Integer (stored in float) coordinates of the probe on the probe grid 
 */
ProbeIndex gridCoordToProbeIndex(in IrradianceField L, in Point3 probeCoords) {
    return int(probeCoords.x + probeCoords.y * L.probeCounts.x + probeCoords.z * L.probeCounts.x * L.probeCounts.y);
}

GridCoord baseGridCoord(in IrradianceField L, Point3 X) {
    return clamp(GridCoord((X - L.probeStartPosition) / L.probeStep),
                GridCoord(0, 0, 0), 
                GridCoord(L.probeCounts) - GridCoord(1, 1, 1));
}

/** Returns the index of the probe at the floor along each dimension. */
ProbeIndex baseProbeIndex(in IrradianceField L, Point3 X) {
    return gridCoordToProbeIndex(L, baseGridCoord(L, X));
}

/** Matches code in LightFieldModel::debugDraw() */
Color3 gridCoordToColor(GridCoord gridCoord) {
    gridCoord.x &= 1;
    gridCoord.y &= 1;
    gridCoord.z &= 1;

    if (gridCoord.x + gridCoord.y + gridCoord.z == 0) {
        return Color3(0.1);
    } else {
        return Color3(gridCoord) * 0.9;
    }
}


GridCoord probeIndexToGridCoord(in IrradianceField L, ProbeIndex index) {    
    /* Works for any # of probes */
    /*
    iPos.x = index % L.probeCounts.x;
    iPos.y = (index % (L.probeCounts.x * L.probeCounts.y)) / L.probeCounts.x;
    iPos.z = index / (L.probeCounts.x * L.probeCounts.y);
    */

    // Assumes probeCounts are powers of two.
    // Saves ~10ms compared to the divisions above
    // Precomputing the MSB actually slows this code down substantially
    ivec3 iPos;
    iPos.x = index & (L.probeCounts.x - 1);
    iPos.y = (index & ((L.probeCounts.x * L.probeCounts.y) - 1)) >> findMSB(L.probeCounts.x);
    iPos.z = index >> findMSB(L.probeCounts.x * L.probeCounts.y);

    return iPos;
}


Color3 probeIndexToColor(in IrradianceField L, ProbeIndex index) {
    return gridCoordToColor(probeIndexToGridCoord(L, index));
}


/** probeCoords Coordinates of the probe, computed as part of the process. */
ProbeIndex nearestProbeIndex(in IrradianceField L, Point3 X, out Point3 probeCoords) {
    probeCoords = clamp(round((X - L.probeStartPosition) / L.probeStep),
                    Point3(0, 0, 0), 
                    Point3(L.probeCounts) - Point3(1, 1, 1));

    return gridCoordToProbeIndex(L, probeCoords);
}

/** 
    \param neighbors The 8 probes surrounding X
    \return Index into the neighbors array of the index of the nearest probe to X 
*/
CycleIndex nearestProbeIndices(in IrradianceField L, Point3 X) {
    Point3 maxProbeCoords = Point3(L.probeCounts) - Point3(1, 1, 1);
    Point3 floatProbeCoords = (X - L.probeStartPosition) / L.probeStep;
    Point3 baseProbeCoords = clamp(floor(floatProbeCoords), Point3(0, 0, 0), maxProbeCoords);

    float minDist = 10.0f;
    int nearestIndex = -1;

    for (int i = 0; i < 8; ++i) {
        Point3 newProbeCoords = min(baseProbeCoords + vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1), maxProbeCoords);
        float d = length(newProbeCoords - floatProbeCoords);
        if (d < minDist) {
            minDist = d;
            nearestIndex = i;
        }
       
    }

    return nearestIndex;
}


Point3 gridCoordToPosition(in IrradianceField L, GridCoord c) {
    return L.probeStep * Vector3(c) + L.probeStartPosition;
}

Point3 probeLocation(in IrradianceField L, ProbeIndex index) {
    return gridCoordToPosition(L, probeIndexToGridCoord(L, index));
}


/** GLSL's dot on ivec3 returns a float. This is an all-integer version */
int idot(ivec3 a, ivec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/**
   \param baseProbeIndex Index into L.radianceProbeGrid's TEXTURE_2D_ARRAY. This is the probe
   at the floor of the current ray sampling position.

   \param relativeIndex on [0, 7]. This is used as a set of three 1-bit offsets

   Returns a probe index into L.radianceProbeGrid. It may be the *same* index as 
   baseProbeIndex.

   This will wrap in crazy ways when the camera is outside of the bounding box
   of the probes...but that's ok. If that case arises, then the trace is likely to 
   be poor quality anyway. Regardless, this function will still return the index 
   of some valid probe, and that probe can either be used or fail because it does not 
   have visibility to the location desired.

   \see nextCycleIndex, baseProbeIndex
 */
ProbeIndex relativeProbeIndex(in IrradianceField L, ProbeIndex baseProbeIndex, CycleIndex relativeIndex) {
    // Guaranteed to be a power of 2
    ProbeIndex numProbes = L.probeCounts.x * L.probeCounts.y * L.probeCounts.z;

    ivec3 offset = ivec3(relativeIndex & 1, (relativeIndex >> 1) & 1, (relativeIndex >> 2) & 1);
    ivec3 stride = ivec3(1, L.probeCounts.x, L.probeCounts.x * L.probeCounts.y);

    return (baseProbeIndex + idot(offset, stride)) & (numProbes - 1);
}


/** Given a CycleIndex [0, 7] on a cube of probes, returns the next CycleIndex to use. 

    \see relativeProbeIndex
*/
CycleIndex nextCycleIndex(CycleIndex cycleIndex) {
    return (cycleIndex + 3) & 7;
}


float squaredLength(Vector3 v) {
    return dot(v, v);
}


/** Two-element sort: maybe swaps a and b so that a' = min(a, b), b' = max(a, b). */
void minSwap(inout float a, inout float b) {
    float temp = min(a, b);
    b = max(a, b);
    a = temp;
}


/** Sort the three values in v from least to 
    greatest using an exchange network (i.e., no branches) */
void sort(inout float3 v) {
    minSwap(v[0], v[1]);
    minSwap(v[1], v[2]);
    minSwap(v[0], v[1]);
}


/** Segments a ray into the piecewise-continuous rays or line segments that each lie within
    one Euclidean octant, which correspond to piecewise-linear projections in octahedral space.
        
    \param boundaryT  all boundary distance ("time") values in units of world-space distance 
      along the ray. In the (common) case where not all five elements are needed, the unused 
      values are all equal to tMax, creating degenerate ray segments.

    \param origin Ray origin in the Euclidean object space of the probe

    \param directionFrac 1 / ray.direction
 */
void computeRaySegments
   (in Point3           origin, 
    in Vector3          directionFrac, 
    in float            tMin,
    in float            tMax,
    out float           boundaryTs[5]) {

    boundaryTs[0] = tMin;
    
    // Time values for intersection with x = 0, y = 0, and z = 0 planes, sorted
    // in increasing order
    Vector3 t = origin * -directionFrac;
    sort(t);

    // Copy the values into the interval boundaries.
    // This loop expands at compile time and eliminates the
    // relative indexing, so it is just three conditional move operations
    for (int i = 0; i < 3; ++i) {
        boundaryTs[i + 1] = clamp(t[i], tMin, tMax);
    }

    boundaryTs[4] = tMax;
}


/** Returns the distance along v from the origin to the intersection 
    with ray R (which it is assumed to intersect) */
float distanceToIntersection(in Ray R, in Vector3 v) {
    //
    //        *-----------X--------------->  R 
    //                    ^
    //                    | v
    //                    |
    //                    O
    //
    // Let X(t) = O + v * t 
    //
    // X is on ray R, so |cross(X - R.origin, R.direction)| ~= 0
    // O = (0, 0, 0)
    //                   |cross(v * t - R.origin, R.direction)| ~= 0
    //
    // (v.y * t - R.origin.y) * R.direction.z = (v.z * t - R.origin.z) * R.direction.y
    // v.y * t * R.direction.z - R.origin.y * R.direction.z = v.z * t * R.direction.y - R.origin.z * R.direction.y 
    // v.y * t * R.direction.z - v.z * t * R.direction.y  =  - R.origin.z * R.direction.y + R.origin.y * R.direction.z
    // t * (v.y * R.direction.z - v.z * R.direction.y)  =  - R.origin.z * R.direction.y + R.origin.y * R.direction.z
    // t = (R.origin.y * R.direction.z - R.origin.z * R.direction.y) / (v.y * R.direction.z - v.z * R.direction.y)
    // [same for other two coordinates]

    float numer;
    float denom = v.y * R.direction.z - v.z * R.direction.y;

    if (abs(denom) > 0.1) {
        numer = R.origin.y * R.direction.z - R.origin.z * R.direction.y;
    } else {
        // We're in the yz plane; use another one
        numer = R.origin.x * R.direction.y - R.origin.y * R.direction.x;
        denom = v.x * R.direction.y - v.y * R.direction.x;
    }

    return numer / denom;
}

#endif