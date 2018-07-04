//
//  Jacobian.h
//  BodyTracking
//
//  Created by Philipp Achenbach on 16.05.18.
//  Copyright © 2018 KTX Software Development. All rights reserved.
//

#ifndef Jacobian_h
#define Jacobian_h

#include "MeshObject.h"
#include "Jacobian/MatrixRmn.h"
#include "RotationUtility.h"

#include <Kore/Log.h>

struct BoneNode;

template<int nJointDOFs = 6, bool posAndOrientation = true> class Jacobian {
    
public:
    std::vector<float> calcDeltaTheta(BoneNode* endEffektor, Kore::vec4 pos_soll, Kore::Quaternion rot_soll, int ikMode = 0) {
        std::vector<float> deltaTheta;
        vec_n vec;
        vec_m deltaP = calcDeltaP(endEffektor, pos_soll, rot_soll);
        mat_mxn jacobian = calcJacobian(endEffektor);
        
        // set error
        error = deltaP.getLength();
        
        switch (ikMode) {
            case 1:
                vec = calcDeltaThetaByPseudoInverse(jacobian, deltaP);
                break;
            case 2:
                vec = calcDeltaThetaByDLS(jacobian, deltaP);
                break;
            case 3:
                vec = calcDeltaThetaBySVD(jacobian, deltaP);
                break;
            case 4:
                vec = calcDeltaThetaByDLSwithSVD(jacobian, deltaP);
                break;
            case 5:
                vec = calcDeltaThetaBySDLS(jacobian, deltaP);
                break;
                
            default:
                vec = calcDeltaThetaByTranspose(jacobian, deltaP);
                break;
        }
        
        for (int n = 0; n < nJointDOFs; ++n)
            deltaTheta.push_back(vec[n]);
        
        return deltaTheta;
    }
    float getError() {
        return error ? error : FLT_MAX;
    }
    
private:
    const float lambdaPseudoInverse = 0.0f;  // Eigentlich 0, da sonst DLS! Bei 0 aber Stabilitätsprobleme!!!
    const float lambdaDLS = 0.18f;           // Lambda für DLS, 0.24 Optimum laut Buss => optimiert!
    const float lambdaSVD = 0.112f;          // Lambda für SVD, 0 bis 1, 0 = alle Werte werden genommen (instabil), 1 = keine Werte
    const float lambdaDLSwithSVD = 0.18f;    // Lambda für DLS with SVD => optimiert!
    const float lambdaSDLS = 0.7853981634f;  // Lambda für SDLS = 45° * PI / 180°
    
    typedef Kore::Matrix<nJointDOFs, posAndOrientation ? 6 : 3, float>                  mat_mxn;
    typedef Kore::Matrix<posAndOrientation ? 6 : 3, nJointDOFs, float>                  mat_nxm;
    typedef Kore::Matrix<posAndOrientation ? 6 : 3, posAndOrientation ? 6 : 3, float>   mat_mxm;
    typedef Kore::Matrix<nJointDOFs, nJointDOFs, float>                                 mat_nxn;
    typedef Kore::Vector<float, posAndOrientation ? 6 : 3>                              vec_m;
    typedef Kore::Vector<float, nJointDOFs>                                             vec_n;
    
    float   error;
    int     nDOFs = posAndOrientation ? 6 : 3;
    
    mat_mxm U;
    mat_nxn V;
    vec_m   d;
    
    vec_n calcDeltaThetaByTranspose(mat_mxn jacobian, vec_m deltaP) {
        return jacobian.Transpose() * deltaP;
    }
    vec_n calcDeltaThetaByPseudoInverse(mat_mxn jacobian, vec_m deltaP) {
        return calcPseudoInverse(jacobian, lambdaPseudoInverse) * deltaP;
    }
    vec_n calcDeltaThetaByDLS(mat_mxn jacobian, vec_m deltaP) {
        return calcPseudoInverse(jacobian, lambdaDLS) * deltaP;
    }
    vec_n calcDeltaThetaBySVD(mat_mxn jacobian, vec_m deltaP) {
        calcSVD(jacobian);
        
        mat_nxm D;
        for (int i = 0; i < Min(nDOFs, nJointDOFs); ++i)
            D[i][i] = fabs(d[i]) > (lambdaSVD * MaxAbs(d)) ? (1 / d[i]) : 0;
        
        return V * D * U.Transpose() * deltaP;
    }
    vec_n calcDeltaThetaByDLSwithSVD(mat_mxn jacobian, vec_m deltaP) {
        calcSVD(jacobian);
        
        mat_nxm E;
        for (int i = 0; i < Min(nDOFs, nJointDOFs); ++i)
            E[i][i] = d[i] / (Square(d[i]) + Square(lambdaDLSwithSVD));
        
        return V * E * U.Transpose() * deltaP;
    }
    vec_n calcDeltaThetaBySDLS(mat_mxn jacobian, vec_m deltaP) {
        calcSVD(jacobian);
        
        vec_n theta;
        for (int i = 0; i < Min(nDOFs, nJointDOFs); ++i) {
            vec_m u_i;
            for (int j = 0; j < nDOFs; ++j) {
                u_i[j] = U[j][i];
            }
            
            vec_n v_i;
            for (int j = 0; j < nJointDOFs; ++j)
                v_i[j] = V[j][i];
            
            // alpha_i = uT_i * deltaP
            float alpha_i = 0.0f;
            for (int m = 0; m < nDOFs; ++m)
                alpha_i += u_i[m] * deltaP[m];
            
            float omegaInverse_i = d[i] != 0 ? 1.0 / d[i] : 0;
            
            float M_i = 0.0f;
            for (int l = 0; l < (nDOFs / 3); ++l)
                for (int j = 0; j < nJointDOFs; ++j)
                    M_i += fabs(V[j][i]) * fabs(jacobian[l][j]);
            M_i *= omegaInverse_i;
            
            float N_i = 1.0; // u_i.getLength();
            float gamma_i = M_i != 0 ? fabs(N_i / M_i) : 0;
            gamma_i = gamma_i < 1 ? gamma_i : 1.0f;
            gamma_i *= lambdaSDLS;
            
            theta += clampMaxAbs(omegaInverse_i * alpha_i * v_i, gamma_i);
        }
        
        return theta;
    }
    
    // ---------------------------------------------------------
    
    vec_m calcDeltaP(BoneNode* endEffektor, Kore::vec3 pos_soll, Kore::Quaternion rot_soll) {
        vec_m deltaP;
        
        // Calculate difference between desired position and actual position of the end effector
        Kore::vec3 deltaPos = pos_soll - calcPosition(endEffektor);
        
        deltaP[0] = deltaPos.x();
        deltaP[1] = deltaPos.y();
        deltaP[2] = deltaPos.z();
        
        // Calculate difference between desired rotation and actual rotation
        if (nDOFs == 6) {
            Kore::Quaternion rot_aktuell;
            Kore::RotationUtility::getOrientation(&endEffektor->combined, &rot_aktuell);
            
            Kore::Quaternion rot_soll_temp = rot_soll;
            rot_soll_temp.normalize();
            
            Kore::Quaternion deltaRot_quat = rot_soll_temp.rotated(rot_aktuell.invert());
            if (deltaRot_quat.w < 0) deltaRot_quat = deltaRot_quat.scaled(-1);
            
            Kore::vec3 deltaRot = Kore::vec3(0, 0, 0);
            Kore::RotationUtility::quatToEuler(&deltaRot_quat, &deltaRot.x(), &deltaRot.y(), &deltaRot.z());
            
            deltaP[3] = deltaRot.x();
            deltaP[4] = deltaRot.y();
            deltaP[5] = deltaRot.z();
        }
        
        return deltaP;
    }
    
    mat_mxn calcJacobian(BoneNode* endEffektor) {
        Jacobian::mat_mxn jacobianMatrix;
        BoneNode* bone = endEffektor;
        
        // Get current rotation and position of the end-effector
        Kore::vec3 p_aktuell = calcPosition(endEffektor);
        
        int joint = 0;
        while (bone->initialized && joint < nJointDOFs) {
            Kore::vec3 axes = bone->axes;
            
            if (axes.x() == 1.0 && joint < nJointDOFs) {
                vec_m column = calcJacobianColumn(bone, p_aktuell, Kore::vec3(1, 0, 0));
                for (int i = 0; i < nDOFs; ++i) jacobianMatrix[i][joint] = column[i];
                joint += 1;
            }
            if (axes.y() == 1.0 && joint < nJointDOFs) {
                vec_m column = calcJacobianColumn(bone, p_aktuell, Kore::vec3(0, 1, 0));
                for (int i = 0; i < nDOFs; ++i) jacobianMatrix[i][joint] = column[i];
                joint += 1;
            }
            if (axes.z() == 1.0 && joint < nJointDOFs) {
                vec_m column = calcJacobianColumn(bone, p_aktuell, Kore::vec3(0, 0, 1));
                for (int i = 0; i < nDOFs; ++i) jacobianMatrix[i][joint] = column[i];
                joint += 1;
            }
            
            bone = bone->parent;
        }
        
        return jacobianMatrix;
    }
    
    vec_m calcJacobianColumn(BoneNode* bone, Kore::vec3 p_aktuell, Kore::vec3 rotAxis) {
        vec_m column;
        
        // Get rotation and position vector of the current bone
        Kore::vec3 p_j = calcPosition(bone);
        
        // get rotation-axis
        Kore::vec4 v_j = bone->combined * Kore::vec4(rotAxis.x(), rotAxis.y(), rotAxis.z(), 0);
        
        // cross-product
        Kore::vec3 pTheta = Kore::vec3(v_j.x(), v_j.y(), v_j.z()).cross(p_aktuell - p_j);
        
        if (nDOFs > 0) column[0] = pTheta.x();
        if (nDOFs > 1) column[1] = pTheta.y();
        if (nDOFs > 2) column[2] = pTheta.z();
        if (nDOFs > 3) column[3] = v_j.x();
        if (nDOFs > 4) column[4] = v_j.y();
        if (nDOFs > 5) column[5] = v_j.z();
        
        return column;
    }
    
    mat_nxm calcPseudoInverse(mat_mxn jacobian, float lambda = 0.0f) { // lambda != 0 => DLS!
        if (nDOFs <= nJointDOFs) { // m <= n
            // Left Damped pseudo-inverse
            return (jacobian.Transpose() * jacobian + Jacobian::mat_nxn::Identity() * Square(lambda)).Invert() * jacobian.Transpose();
        } else {
            // Right Damped pseudo-inverse
            return jacobian.Transpose() * (jacobian * jacobian.Transpose() + Jacobian::mat_mxm::Identity() * Square(lambda)).Invert();
        }
    }
    
    Kore::vec3 calcPosition(BoneNode* bone) {
        Kore::vec3 result;
        
        // from quat to euler!
        Kore::vec4 quat = bone->combined * Kore::vec4(0, 0, 0, 1);
        quat *= 1.0 / quat.w();
        
        result.x() = quat.x();
        result.y() = quat.y();
        result.z() = quat.z();
        
        return result;
    }
    
    void calcSVD(Jacobian::mat_mxn jacobian) {
        MatrixRmn J = MatrixRmn(nDOFs, nJointDOFs);
        MatrixRmn U = MatrixRmn(nDOFs, nDOFs);
        MatrixRmn V = MatrixRmn(nJointDOFs, nJointDOFs);
        VectorRn d = VectorRn(Min(nDOFs, nJointDOFs));
        
        for (int m = 0; m < nDOFs; ++m)
            for (int n = 0; n < nJointDOFs; ++n)
                J.Set(m, n, (double) jacobian[m][n]);
        
        J.ComputeSVD(U, d, V);
        assert(J.DebugCheckSVD(U, d , V));
        
        for (int m = 0; m < Max(nDOFs, nJointDOFs); ++m) {
            for (int n = 0; n < Max(nDOFs, nJointDOFs); ++n) {
                if (m < nDOFs && n < nDOFs)
                    Jacobian::U[m][n] = (float) U.Get(m, n);
                
                if (m < nJointDOFs && n < nJointDOFs)
                    Jacobian::V[m][n] = (float) V.Get(m, n);
                
                if (m == n && m < Min(nDOFs, nJointDOFs))
                    Jacobian::d[m] = (float) d.Get(m);
            }
        }
    }
    
    vec_n clampMaxAbs(vec_n vec, float gamma_i) {
        float maxValue = MaxAbs(vec, gamma_i);
        
        // scale vector to gamma_i as max
        if (maxValue != gamma_i)
            for (int n = 0; n < nJointDOFs; ++n)
                vec[n] = vec[n] / maxValue * gamma_i;
        
        return vec;
    }
    
    float MaxAbs(vec_m vec) {
        float result = 0.0f;
        
        for (int m = 0; m < nDOFs; ++m) {
            float temp = fabs(vec[m]);
                              
            if (temp > result) result = temp;
        }
        
        return result;
    }
    
    float MaxAbs(vec_n vec, float start) {
        float result = start;
        
        for (int n = 0; n < nDOFs; ++n) {
            float temp = fabs(vec[n]);
            
            if (temp > result) result = temp;
        }
        
        return result;
    }
};

#endif /* Jacobian_h */