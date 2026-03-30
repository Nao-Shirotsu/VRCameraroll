#pragma once
#include <cmath>
#include <openvr.h>

// 3x3 回転行列（ポインターラインのローカル軸回転用）
struct Mat3 {
    float m[3][3] = {};
    static Mat3 identity() {
        Mat3 r; r.m[0][0]=r.m[1][1]=r.m[2][2]=1.f; return r;
    }
    static Mat3 rotX(float a) {
        Mat3 r = identity();
        r.m[1][1]= cosf(a); r.m[1][2]=-sinf(a);
        r.m[2][1]= sinf(a); r.m[2][2]= cosf(a);
        return r;
    }
    static Mat3 rotY(float a) {
        Mat3 r = identity();
        r.m[0][0]= cosf(a); r.m[0][2]= sinf(a);
        r.m[2][0]=-sinf(a); r.m[2][2]= cosf(a);
        return r;
    }
    static Mat3 rotZ(float a) {
        Mat3 r = identity();
        r.m[0][0]= cosf(a); r.m[0][1]=-sinf(a);
        r.m[1][0]= sinf(a); r.m[1][1]= cosf(a);
        return r;
    }
    Mat3 operator*(const Mat3& o) const {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }
};

// コントローラー空間での外因的回転 + オフセット平行移動
// R = Rz(rotZ) * Ry(rotY) * Rx(rotX)
// 位置 = R * [x, y, z]^T + [offsetX, offsetY, offsetZ]^T
static inline vr::HmdMatrix34_t MakeTransform(
    float x,  float y,  float z,
    float rotX, float rotY, float rotZ,
    float offsetX, float offsetY, float offsetZ)
{
    const float cx = cosf(rotX), sx = sinf(rotX);
    const float cy = cosf(rotY), sy = sinf(rotY);
    const float cz = cosf(rotZ), sz = sinf(rotZ);

    const float r00 = cz*cy,   r01 = cz*sy*sx - sz*cx,  r02 = cz*sy*cx + sz*sx;
    const float r10 = sz*cy,   r11 = sz*sy*sx + cz*cx,  r12 = sz*sy*cx - cz*sx;
    const float r20 = -sy,     r21 = cy*sx,              r22 = cy*cx;

    vr::HmdMatrix34_t t = {};
    t.m[0][0]=r00; t.m[0][1]=r01; t.m[0][2]=r02;
    t.m[1][0]=r10; t.m[1][1]=r11; t.m[1][2]=r12;
    t.m[2][0]=r20; t.m[2][1]=r21; t.m[2][2]=r22;
    t.m[0][3] = r00*x + r01*y + r02*z + offsetX;
    t.m[1][3] = r10*x + r11*y + r12*z + offsetY;
    t.m[2][3] = r20*x + r21*y + r22*z + offsetZ;
    return t;
}
