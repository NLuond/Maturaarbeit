#pragma once
#include <math.h>
#include <Arduino.h>

class MadgwickAHRS {
public:
    float q[4] = {1.f, 0.f, 0.f, 0.f};
    float beta;
    explicit MadgwickAHRS(float b = 0.033f) : beta(b) {}

    void update(float Gx, float Gy, float Gz, float Ax, float Ay, float Az, float dt) {
        static const float D2R = 0.017453293f;
        Gx *= D2R; Gy *= D2R; Gz *= D2R;
        float q0=q[0], q1=q[1], q2=q[2], q3=q[3];
        float qd0 = 0.5f*(-q1*Gx - q2*Gy - q3*Gz);
        float qd1 = 0.5f*( q0*Gx + q2*Gz - q3*Gy);
        float qd2 = 0.5f*( q0*Gy - q1*Gz + q3*Gx);
        float qd3 = 0.5f*( q0*Gz + q1*Gy - q2*Gx);
        float an = Ax*Ax + Ay*Ay + Az*Az;
        if (an > 1e-4f) {
            float rn = 1.f / sqrtf(an);
            Ax *= rn; Ay *= rn; Az *= rn;
            float q0q0=q0*q0, q1q1=q1*q1, q2q2=q2*q2, q3q3=q3*q3;
            float _2q0=2*q0, _2q1=2*q1, _2q2=2*q2, _2q3=2*q3;
            float _4q0=4*q0, _4q1=4*q1, _4q2=4*q2;
            float _8q1=8*q1, _8q2=8*q2;
            float s0 = _4q0*q2q2 + _2q2*Ax + _4q0*q1q1 - _2q1*Ay;
            float s1 = _4q1*q3q3 - _2q3*Ax + 4*q0q0*q1 - _2q0*Ay - _4q1 + _8q1*q1q1 + _8q1*q2q2 + _4q1*Az;
            float s2 = 4*q0q0*q2 + _2q0*Ax + _4q2*q3q3 - _2q3*Ay - _4q2 + _8q2*q1q1 + _8q2*q2q2 + _4q2*Az;
            float s3 = 4*q1q1*q3 - _2q1*Ax + 4*q2q2*q3 - _2q2*Ay;
            float sn = s0*s0 + s1*s1 + s2*s2 + s3*s3;
            if (sn > 1e-10f) {
                float rs = beta / sqrtf(sn);
                qd0 -= rs*s0; qd1 -= rs*s1; qd2 -= rs*s2; qd3 -= rs*s3;
            }
        }
        q0 += qd0*dt; q1 += qd1*dt; q2 += qd2*dt; q3 += qd3*dt;
        float rn = 1.f / sqrtf(q0*q0+q1*q1+q2*q2+q3*q3);
        q[0]=q0*rn; q[1]=q1*rn; q[2]=q2*rn; q[3]=q3*rn;
    }
    float pitchDeg() const {
        return asinf(constrain(2.f*(q[0]*q[2]-q[3]*q[1]), -1.f, 1.f)) * 57.29578f;
    }
};