/**
 * @file    maths.h
 * @brief   Scalar helpers, 3-vectors and quaternions (single precision).
 *
 * Frames: body = FRD (x forward, y right, z down), earth = NED.
 * Quaternions rotate body -> earth: v_earth = q * v_body * q^-1.
 */
#ifndef MATHS_H
#define MATHS_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define M_PI_F          3.14159265358979f
#define DEG2RAD         0.0174532925199433f
#define RAD2DEG         57.2957795130823f
#define GRAVITY_MSS     9.80665f

static inline float constrainf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int32_t constraini(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float sqf(float x) { return x * x; }

static inline float signf(float x) { return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f); }

/** Symmetric dead band that keeps the output continuous: |v| <= db -> 0. */
static inline float deadbandf(float v, float db)
{
    if (v > db) {
        return v - db;
    }
    if (v < -db) {
        return v + db;
    }
    return 0.0f;
}

static inline bool is_finitef(float x) { return isfinite(x) != 0; }

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* ------------------------------------------------------------------------ */
typedef struct {
    float x, y, z;
} vec3_t;

static inline vec3_t v3(float x, float y, float z) { vec3_t r = { x, y, z }; return r; }
static inline vec3_t v3_add(vec3_t a, vec3_t b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3_t v3_sub(vec3_t a, vec3_t b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3_t v3_scale(vec3_t a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float  v3_dot(vec3_t a, vec3_t b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3_t v3_cross(vec3_t a, vec3_t b)
{
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float  v3_norm(vec3_t a) { return sqrtf(v3_dot(a, a)); }
static inline float  v3_get(vec3_t a, int i) { return (i == 0) ? a.x : ((i == 1) ? a.y : a.z); }

/* ------------------------------------------------------------------------ */
typedef struct {
    float w, x, y, z;
} quat_t;

static inline quat_t quat_identity(void) { quat_t q = { 1.0f, 0.0f, 0.0f, 0.0f }; return q; }

static inline quat_t quat_normalize(quat_t q)
{
    float n = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-9f) {
        return quat_identity();
    }
    float s = 1.0f / n;
    quat_t r = { q.w * s, q.x * s, q.y * s, q.z * s };
    return r;
}

/** Quaternion from ZYX Euler angles (rad): roll phi, pitch theta, yaw psi. */
static inline quat_t quat_from_euler(float roll, float pitch, float yaw)
{
    float cr = cosf(roll * 0.5f), sr = sinf(roll * 0.5f);
    float cp = cosf(pitch * 0.5f), sp = sinf(pitch * 0.5f);
    float cy = cosf(yaw * 0.5f), sy = sinf(yaw * 0.5f);
    quat_t q = {
        cr * cp * cy + sr * sp * sy,
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
    };
    return q;
}

static inline void quat_to_euler(quat_t q, float *roll, float *pitch, float *yaw)
{
    *roll = atan2f(2.0f * (q.w * q.x + q.y * q.z), 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    *pitch = asinf(constrainf(2.0f * (q.w * q.y - q.z * q.x), -1.0f, 1.0f));
    *yaw = atan2f(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

/** Rotation matrix body -> earth (row-major). */
static inline void quat_to_dcm(quat_t q, float r[3][3])
{
    float ww = q.w * q.w, xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r[0][0] = ww + xx - yy - zz;  r[0][1] = 2.0f * (xy - wz);   r[0][2] = 2.0f * (xz + wy);
    r[1][0] = 2.0f * (xy + wz);   r[1][1] = ww - xx + yy - zz;  r[1][2] = 2.0f * (yz - wx);
    r[2][0] = 2.0f * (xz - wy);   r[2][1] = 2.0f * (yz + wx);   r[2][2] = ww - xx - yy + zz;
}

#endif /* MATHS_H */
