#ifndef pathTraceHelpers_h
#define pathTraceHelpers_h

#include <compatibility.glsl>
#include <g3dmath.glsl>
#define PI 3.14159265


/** 
    Port of CoordinateSystem() from pbrt v2 
    (https://github.com/mmp/pbrt-v2/blob/master/src/core/geometry.h)
*/
void CoordinateSystem(in Vector3 v1, out Vector3 v2, out Vector3 v3) {
    if (abs(v1.x) > abs(v1.y)) {
        float invLen = 1.f / sqrt(v1.x*v1.x + v1.z*v1.z);
        v2 = vec3(-v1.z * invLen, 0.f, v1.x * invLen);
    } else {
        float invLen = 1.f / sqrt(v1.y*v1.y + v1.z*v1.z);
        v2 = vec3(0.f, v1.z * invLen, -v1.y * invLen);
    }
    v3 = cross(v1, v2);
}


Matrix3 fromAxisAngle(Vector3 axis, float fRadians) {
    Matrix3 m;
    float fCos = cos(fRadians);
    float fSin = sin(fRadians);
    float fOneMinusCos = 1.0f - fCos;
    float fX2 = square(axis.x);
    float fY2 = square(axis.y);
    float fZ2 = square(axis.z);
    float fXYM = axis.x * axis.y * fOneMinusCos;
    float fXZM = axis.x * axis.z * fOneMinusCos;
    float fYZM = axis.y * axis.z * fOneMinusCos;
    float fXSin = axis.x * fSin;
    float fYSin = axis.y * fSin;
    float fZSin = axis.z * fSin;

    m[0][0] = fX2 * fOneMinusCos + fCos;
    m[1][0] = fXYM - fZSin;
    m[2][0] = fXZM + fYSin;

    m[0][1] = fXYM + fZSin;
    m[1][1] = fY2 * fOneMinusCos + fCos;
    m[2][1] = fYZM - fXSin;

    m[0][2] = fXZM - fYSin;
    m[1][2] = fYZM + fXSin;
    m[2][2] = fZ2 * fOneMinusCos + fCos;
    return m;
}

Matrix3 rotMatrixFromZVector(in Vector3 zVec, float rand) {
    Vector3 xVec, yVec;
    CoordinateSystem(zVec, xVec, yVec);
    float gamma = rand * 2 * PI;
    Matrix3 rotAboutZ = fromAxisAngle(zVec, gamma);

    return Matrix3(rotAboutZ*xVec, rotAboutZ*yVec, zVec);
}

Vector3 rotatedSphericalFibonacci(Matrix3 rotation, float i, float n) {
    return rotation * sphericalFibonacci(i, n);
}

#endif