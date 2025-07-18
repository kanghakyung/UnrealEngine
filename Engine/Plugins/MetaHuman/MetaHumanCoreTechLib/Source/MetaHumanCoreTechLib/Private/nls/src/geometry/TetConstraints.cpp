// Copyright Epic Games, Inc. All Rights Reserved.

#include <nls/geometry/TetConstraints.h>
#include <nls/math/Math.h>
#include <carbon/utils/Timer.h>
#include <iostream>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
void FtoR(const Eigen::Matrix<T, 3, 3>& F, Eigen::Matrix<T, 3, 3>& R, Eigen::Matrix<T, 9, 9>* dRdF)
{
    Eigen::Matrix<T, 3, 3> S;
    const Eigen::JacobiSVD<Eigen::Matrix<T, 3, 3>> svd(F, Eigen::ComputeFullU | Eigen::ComputeFullV);
    const T det = F.determinant();
    if (det < 0)
    {
        R = svd.matrixU() * Eigen::Vector<T, 3>(1, 1, -1).asDiagonal() * svd.matrixV().transpose();
    }
    else
    {
        R = svd.matrixU() * svd.matrixV().transpose();
    }

    if (dRdF)
    {
        if (det < 0)
        {
            S = svd.matrixV() * Eigen::Vector<T, 3>(1, 1, -1).asDiagonal() * svd.singularValues().asDiagonal() * svd.matrixV().transpose();
        }
        else
        {
            S = svd.matrixV() * svd.singularValues().asDiagonal() * svd.matrixV().transpose();
        }

        const Eigen::Matrix<T, 3, 3> D = S.trace() * Eigen::Matrix<T, 3, 3>::Identity() - S;
        Eigen::Matrix<T, 3, 3> Dinv;
        if (D.determinant() != 0)
        {
            Dinv = D.inverse();
        }
        else
        {
            Dinv = Eigen::Matrix<T, 3, 3>::Zero();
        }

        // Eigen::Matrix<T, 3, 3> E[3];
        // E[0] = Eigen::Matrix<T, 3, 3>::Zero();
        // E[0](2, 1) = -1;
        // E[0](1, 2) = 1;
        // E[1] = Eigen::Matrix<T, 3, 3>::Zero();
        // E[1](2, 0) = 1;
        // E[1](0, 2) = -1;
        // E[2] = Eigen::Matrix<T, 3, 3>::Zero();
        // E[2](1, 0) = -1;
        // E[2](0, 1) = 1;

        // for (int d = 0; d < 3; ++d) {
        // for (int y = 0; y < 3; ++y) {
        // for (int t = 0; t < 3; ++t) {
        // for (int p = 0; p < 3; ++p) {
        // std::vector<std::string> valuesToAdd;
        // T deriv = 0;
        // for (int a = 0; a < 3; ++a) {
        // for (int b = 0; b < 3; ++b) {
        // for (int e = 0; e < 3; ++e) {
        // for (int s = 0; s < 3; ++s) {
        // const T mul = E[a](e,d) * E[b](s,t);
        // if (mul != 0) {
        // deriv += E[a](e,d) * R(y,e) * Dinv(a,b) * E[b](s,t) * R(p, s);
        ////LOG_INFO("({},{}) += R({},{}) * Dinv({},{} * R({},{}))", 3 * d + y, 3 * t + p, y, e, a, b, p, s);
        // valuesToAdd.push_back(TITAN_NAMESPACE::fmt::format("{} R({},{}) * Dinv({},{}) * R({},{})", mul < 0 ? " -" : " +", y, e, a, b, p, s));
        // }
        // }
        // }
        // }
        // }
        // std::string command = TITAN_NAMESPACE::fmt::format("(*dRdF)({},{}) =", 3 * d + y, 3 * t + p);
        // for (size_t k = 0; k < valuesToAdd.size(); ++k) {
        // command = command + valuesToAdd[k];
        // }
        // printf("%s;\n", command.c_str());
        // (*dRdF)(3 * d + y, 3 * t + p) = deriv;
        // }
        // }
        // }
        // }

        (*dRdF)(0, 0) = +R(0, 2) * Dinv(1, 1) * R(0, 2) - R(0, 2) * Dinv(1, 2) * R(0, 1) - R(0, 1) * Dinv(2, 1) * R(0, 2) + R(0, 1) * Dinv(2, 2) * R(0, 1);
        (*dRdF)(0, 1) = +R(0, 2) * Dinv(1, 1) * R(1, 2) - R(0, 2) * Dinv(1, 2) * R(1, 1) - R(0, 1) * Dinv(2, 1) * R(1, 2) + R(0, 1) * Dinv(2, 2) * R(1, 1);
        (*dRdF)(0, 2) = +R(0, 2) * Dinv(1, 1) * R(2, 2) - R(0, 2) * Dinv(1, 2) * R(2, 1) - R(0, 1) * Dinv(2, 1) * R(2, 2) + R(0, 1) * Dinv(2, 2) * R(2, 1);
        (*dRdF)(0, 3) = -R(0, 2) * Dinv(1, 0) * R(0, 2) + R(0, 2) * Dinv(1, 2) * R(0, 0) + R(0, 1) * Dinv(2, 0) * R(0, 2) - R(0, 1) * Dinv(2, 2) * R(0, 0);
        (*dRdF)(0, 4) = -R(0, 2) * Dinv(1, 0) * R(1, 2) + R(0, 2) * Dinv(1, 2) * R(1, 0) + R(0, 1) * Dinv(2, 0) * R(1, 2) - R(0, 1) * Dinv(2, 2) * R(1, 0);
        (*dRdF)(0, 5) = -R(0, 2) * Dinv(1, 0) * R(2, 2) + R(0, 2) * Dinv(1, 2) * R(2, 0) + R(0, 1) * Dinv(2, 0) * R(2, 2) - R(0, 1) * Dinv(2, 2) * R(2, 0);
        (*dRdF)(0, 6) = +R(0, 2) * Dinv(1, 0) * R(0, 1) - R(0, 2) * Dinv(1, 1) * R(0, 0) - R(0, 1) * Dinv(2, 0) * R(0, 1) + R(0, 1) * Dinv(2, 1) * R(0, 0);
        (*dRdF)(0, 7) = +R(0, 2) * Dinv(1, 0) * R(1, 1) - R(0, 2) * Dinv(1, 1) * R(1, 0) - R(0, 1) * Dinv(2, 0) * R(1, 1) + R(0, 1) * Dinv(2, 1) * R(1, 0);
        (*dRdF)(0, 8) = +R(0, 2) * Dinv(1, 0) * R(2, 1) - R(0, 2) * Dinv(1, 1) * R(2, 0) - R(0, 1) * Dinv(2, 0) * R(2, 1) + R(0, 1) * Dinv(2, 1) * R(2, 0);
        (*dRdF)(1, 0) = +R(1, 2) * Dinv(1, 1) * R(0, 2) - R(1, 2) * Dinv(1, 2) * R(0, 1) - R(1, 1) * Dinv(2, 1) * R(0, 2) + R(1, 1) * Dinv(2, 2) * R(0, 1);
        (*dRdF)(1, 1) = +R(1, 2) * Dinv(1, 1) * R(1, 2) - R(1, 2) * Dinv(1, 2) * R(1, 1) - R(1, 1) * Dinv(2, 1) * R(1, 2) + R(1, 1) * Dinv(2, 2) * R(1, 1);
        (*dRdF)(1, 2) = +R(1, 2) * Dinv(1, 1) * R(2, 2) - R(1, 2) * Dinv(1, 2) * R(2, 1) - R(1, 1) * Dinv(2, 1) * R(2, 2) + R(1, 1) * Dinv(2, 2) * R(2, 1);
        (*dRdF)(1, 3) = -R(1, 2) * Dinv(1, 0) * R(0, 2) + R(1, 2) * Dinv(1, 2) * R(0, 0) + R(1, 1) * Dinv(2, 0) * R(0, 2) - R(1, 1) * Dinv(2, 2) * R(0, 0);
        (*dRdF)(1, 4) = -R(1, 2) * Dinv(1, 0) * R(1, 2) + R(1, 2) * Dinv(1, 2) * R(1, 0) + R(1, 1) * Dinv(2, 0) * R(1, 2) - R(1, 1) * Dinv(2, 2) * R(1, 0);
        (*dRdF)(1, 5) = -R(1, 2) * Dinv(1, 0) * R(2, 2) + R(1, 2) * Dinv(1, 2) * R(2, 0) + R(1, 1) * Dinv(2, 0) * R(2, 2) - R(1, 1) * Dinv(2, 2) * R(2, 0);
        (*dRdF)(1, 6) = +R(1, 2) * Dinv(1, 0) * R(0, 1) - R(1, 2) * Dinv(1, 1) * R(0, 0) - R(1, 1) * Dinv(2, 0) * R(0, 1) + R(1, 1) * Dinv(2, 1) * R(0, 0);
        (*dRdF)(1, 7) = +R(1, 2) * Dinv(1, 0) * R(1, 1) - R(1, 2) * Dinv(1, 1) * R(1, 0) - R(1, 1) * Dinv(2, 0) * R(1, 1) + R(1, 1) * Dinv(2, 1) * R(1, 0);
        (*dRdF)(1, 8) = +R(1, 2) * Dinv(1, 0) * R(2, 1) - R(1, 2) * Dinv(1, 1) * R(2, 0) - R(1, 1) * Dinv(2, 0) * R(2, 1) + R(1, 1) * Dinv(2, 1) * R(2, 0);
        (*dRdF)(2, 0) = +R(2, 2) * Dinv(1, 1) * R(0, 2) - R(2, 2) * Dinv(1, 2) * R(0, 1) - R(2, 1) * Dinv(2, 1) * R(0, 2) + R(2, 1) * Dinv(2, 2) * R(0, 1);
        (*dRdF)(2, 1) = +R(2, 2) * Dinv(1, 1) * R(1, 2) - R(2, 2) * Dinv(1, 2) * R(1, 1) - R(2, 1) * Dinv(2, 1) * R(1, 2) + R(2, 1) * Dinv(2, 2) * R(1, 1);
        (*dRdF)(2, 2) = +R(2, 2) * Dinv(1, 1) * R(2, 2) - R(2, 2) * Dinv(1, 2) * R(2, 1) - R(2, 1) * Dinv(2, 1) * R(2, 2) + R(2, 1) * Dinv(2, 2) * R(2, 1);
        (*dRdF)(2, 3) = -R(2, 2) * Dinv(1, 0) * R(0, 2) + R(2, 2) * Dinv(1, 2) * R(0, 0) + R(2, 1) * Dinv(2, 0) * R(0, 2) - R(2, 1) * Dinv(2, 2) * R(0, 0);
        (*dRdF)(2, 4) = -R(2, 2) * Dinv(1, 0) * R(1, 2) + R(2, 2) * Dinv(1, 2) * R(1, 0) + R(2, 1) * Dinv(2, 0) * R(1, 2) - R(2, 1) * Dinv(2, 2) * R(1, 0);
        (*dRdF)(2, 5) = -R(2, 2) * Dinv(1, 0) * R(2, 2) + R(2, 2) * Dinv(1, 2) * R(2, 0) + R(2, 1) * Dinv(2, 0) * R(2, 2) - R(2, 1) * Dinv(2, 2) * R(2, 0);
        (*dRdF)(2, 6) = +R(2, 2) * Dinv(1, 0) * R(0, 1) - R(2, 2) * Dinv(1, 1) * R(0, 0) - R(2, 1) * Dinv(2, 0) * R(0, 1) + R(2, 1) * Dinv(2, 1) * R(0, 0);
        (*dRdF)(2, 7) = +R(2, 2) * Dinv(1, 0) * R(1, 1) - R(2, 2) * Dinv(1, 1) * R(1, 0) - R(2, 1) * Dinv(2, 0) * R(1, 1) + R(2, 1) * Dinv(2, 1) * R(1, 0);
        (*dRdF)(2, 8) = +R(2, 2) * Dinv(1, 0) * R(2, 1) - R(2, 2) * Dinv(1, 1) * R(2, 0) - R(2, 1) * Dinv(2, 0) * R(2, 1) + R(2, 1) * Dinv(2, 1) * R(2, 0);
        (*dRdF)(3, 0) = -R(0, 2) * Dinv(0, 1) * R(0, 2) + R(0, 2) * Dinv(0, 2) * R(0, 1) + R(0, 0) * Dinv(2, 1) * R(0, 2) - R(0, 0) * Dinv(2, 2) * R(0, 1);
        (*dRdF)(3, 1) = -R(0, 2) * Dinv(0, 1) * R(1, 2) + R(0, 2) * Dinv(0, 2) * R(1, 1) + R(0, 0) * Dinv(2, 1) * R(1, 2) - R(0, 0) * Dinv(2, 2) * R(1, 1);
        (*dRdF)(3, 2) = -R(0, 2) * Dinv(0, 1) * R(2, 2) + R(0, 2) * Dinv(0, 2) * R(2, 1) + R(0, 0) * Dinv(2, 1) * R(2, 2) - R(0, 0) * Dinv(2, 2) * R(2, 1);
        (*dRdF)(3, 3) = +R(0, 2) * Dinv(0, 0) * R(0, 2) - R(0, 2) * Dinv(0, 2) * R(0, 0) - R(0, 0) * Dinv(2, 0) * R(0, 2) + R(0, 0) * Dinv(2, 2) * R(0, 0);
        (*dRdF)(3, 4) = +R(0, 2) * Dinv(0, 0) * R(1, 2) - R(0, 2) * Dinv(0, 2) * R(1, 0) - R(0, 0) * Dinv(2, 0) * R(1, 2) + R(0, 0) * Dinv(2, 2) * R(1, 0);
        (*dRdF)(3, 5) = +R(0, 2) * Dinv(0, 0) * R(2, 2) - R(0, 2) * Dinv(0, 2) * R(2, 0) - R(0, 0) * Dinv(2, 0) * R(2, 2) + R(0, 0) * Dinv(2, 2) * R(2, 0);
        (*dRdF)(3, 6) = -R(0, 2) * Dinv(0, 0) * R(0, 1) + R(0, 2) * Dinv(0, 1) * R(0, 0) + R(0, 0) * Dinv(2, 0) * R(0, 1) - R(0, 0) * Dinv(2, 1) * R(0, 0);
        (*dRdF)(3, 7) = -R(0, 2) * Dinv(0, 0) * R(1, 1) + R(0, 2) * Dinv(0, 1) * R(1, 0) + R(0, 0) * Dinv(2, 0) * R(1, 1) - R(0, 0) * Dinv(2, 1) * R(1, 0);
        (*dRdF)(3, 8) = -R(0, 2) * Dinv(0, 0) * R(2, 1) + R(0, 2) * Dinv(0, 1) * R(2, 0) + R(0, 0) * Dinv(2, 0) * R(2, 1) - R(0, 0) * Dinv(2, 1) * R(2, 0);
        (*dRdF)(4, 0) = -R(1, 2) * Dinv(0, 1) * R(0, 2) + R(1, 2) * Dinv(0, 2) * R(0, 1) + R(1, 0) * Dinv(2, 1) * R(0, 2) - R(1, 0) * Dinv(2, 2) * R(0, 1);
        (*dRdF)(4, 1) = -R(1, 2) * Dinv(0, 1) * R(1, 2) + R(1, 2) * Dinv(0, 2) * R(1, 1) + R(1, 0) * Dinv(2, 1) * R(1, 2) - R(1, 0) * Dinv(2, 2) * R(1, 1);
        (*dRdF)(4, 2) = -R(1, 2) * Dinv(0, 1) * R(2, 2) + R(1, 2) * Dinv(0, 2) * R(2, 1) + R(1, 0) * Dinv(2, 1) * R(2, 2) - R(1, 0) * Dinv(2, 2) * R(2, 1);
        (*dRdF)(4, 3) = +R(1, 2) * Dinv(0, 0) * R(0, 2) - R(1, 2) * Dinv(0, 2) * R(0, 0) - R(1, 0) * Dinv(2, 0) * R(0, 2) + R(1, 0) * Dinv(2, 2) * R(0, 0);
        (*dRdF)(4, 4) = +R(1, 2) * Dinv(0, 0) * R(1, 2) - R(1, 2) * Dinv(0, 2) * R(1, 0) - R(1, 0) * Dinv(2, 0) * R(1, 2) + R(1, 0) * Dinv(2, 2) * R(1, 0);
        (*dRdF)(4, 5) = +R(1, 2) * Dinv(0, 0) * R(2, 2) - R(1, 2) * Dinv(0, 2) * R(2, 0) - R(1, 0) * Dinv(2, 0) * R(2, 2) + R(1, 0) * Dinv(2, 2) * R(2, 0);
        (*dRdF)(4, 6) = -R(1, 2) * Dinv(0, 0) * R(0, 1) + R(1, 2) * Dinv(0, 1) * R(0, 0) + R(1, 0) * Dinv(2, 0) * R(0, 1) - R(1, 0) * Dinv(2, 1) * R(0, 0);
        (*dRdF)(4, 7) = -R(1, 2) * Dinv(0, 0) * R(1, 1) + R(1, 2) * Dinv(0, 1) * R(1, 0) + R(1, 0) * Dinv(2, 0) * R(1, 1) - R(1, 0) * Dinv(2, 1) * R(1, 0);
        (*dRdF)(4, 8) = -R(1, 2) * Dinv(0, 0) * R(2, 1) + R(1, 2) * Dinv(0, 1) * R(2, 0) + R(1, 0) * Dinv(2, 0) * R(2, 1) - R(1, 0) * Dinv(2, 1) * R(2, 0);
        (*dRdF)(5, 0) = -R(2, 2) * Dinv(0, 1) * R(0, 2) + R(2, 2) * Dinv(0, 2) * R(0, 1) + R(2, 0) * Dinv(2, 1) * R(0, 2) - R(2, 0) * Dinv(2, 2) * R(0, 1);
        (*dRdF)(5, 1) = -R(2, 2) * Dinv(0, 1) * R(1, 2) + R(2, 2) * Dinv(0, 2) * R(1, 1) + R(2, 0) * Dinv(2, 1) * R(1, 2) - R(2, 0) * Dinv(2, 2) * R(1, 1);
        (*dRdF)(5, 2) = -R(2, 2) * Dinv(0, 1) * R(2, 2) + R(2, 2) * Dinv(0, 2) * R(2, 1) + R(2, 0) * Dinv(2, 1) * R(2, 2) - R(2, 0) * Dinv(2, 2) * R(2, 1);
        (*dRdF)(5, 3) = +R(2, 2) * Dinv(0, 0) * R(0, 2) - R(2, 2) * Dinv(0, 2) * R(0, 0) - R(2, 0) * Dinv(2, 0) * R(0, 2) + R(2, 0) * Dinv(2, 2) * R(0, 0);
        (*dRdF)(5, 4) = +R(2, 2) * Dinv(0, 0) * R(1, 2) - R(2, 2) * Dinv(0, 2) * R(1, 0) - R(2, 0) * Dinv(2, 0) * R(1, 2) + R(2, 0) * Dinv(2, 2) * R(1, 0);
        (*dRdF)(5, 5) = +R(2, 2) * Dinv(0, 0) * R(2, 2) - R(2, 2) * Dinv(0, 2) * R(2, 0) - R(2, 0) * Dinv(2, 0) * R(2, 2) + R(2, 0) * Dinv(2, 2) * R(2, 0);
        (*dRdF)(5, 6) = -R(2, 2) * Dinv(0, 0) * R(0, 1) + R(2, 2) * Dinv(0, 1) * R(0, 0) + R(2, 0) * Dinv(2, 0) * R(0, 1) - R(2, 0) * Dinv(2, 1) * R(0, 0);
        (*dRdF)(5, 7) = -R(2, 2) * Dinv(0, 0) * R(1, 1) + R(2, 2) * Dinv(0, 1) * R(1, 0) + R(2, 0) * Dinv(2, 0) * R(1, 1) - R(2, 0) * Dinv(2, 1) * R(1, 0);
        (*dRdF)(5, 8) = -R(2, 2) * Dinv(0, 0) * R(2, 1) + R(2, 2) * Dinv(0, 1) * R(2, 0) + R(2, 0) * Dinv(2, 0) * R(2, 1) - R(2, 0) * Dinv(2, 1) * R(2, 0);
        (*dRdF)(6, 0) = +R(0, 1) * Dinv(0, 1) * R(0, 2) - R(0, 1) * Dinv(0, 2) * R(0, 1) - R(0, 0) * Dinv(1, 1) * R(0, 2) + R(0, 0) * Dinv(1, 2) * R(0, 1);
        (*dRdF)(6, 1) = +R(0, 1) * Dinv(0, 1) * R(1, 2) - R(0, 1) * Dinv(0, 2) * R(1, 1) - R(0, 0) * Dinv(1, 1) * R(1, 2) + R(0, 0) * Dinv(1, 2) * R(1, 1);
        (*dRdF)(6, 2) = +R(0, 1) * Dinv(0, 1) * R(2, 2) - R(0, 1) * Dinv(0, 2) * R(2, 1) - R(0, 0) * Dinv(1, 1) * R(2, 2) + R(0, 0) * Dinv(1, 2) * R(2, 1);
        (*dRdF)(6, 3) = -R(0, 1) * Dinv(0, 0) * R(0, 2) + R(0, 1) * Dinv(0, 2) * R(0, 0) + R(0, 0) * Dinv(1, 0) * R(0, 2) - R(0, 0) * Dinv(1, 2) * R(0, 0);
        (*dRdF)(6, 4) = -R(0, 1) * Dinv(0, 0) * R(1, 2) + R(0, 1) * Dinv(0, 2) * R(1, 0) + R(0, 0) * Dinv(1, 0) * R(1, 2) - R(0, 0) * Dinv(1, 2) * R(1, 0);
        (*dRdF)(6, 5) = -R(0, 1) * Dinv(0, 0) * R(2, 2) + R(0, 1) * Dinv(0, 2) * R(2, 0) + R(0, 0) * Dinv(1, 0) * R(2, 2) - R(0, 0) * Dinv(1, 2) * R(2, 0);
        (*dRdF)(6, 6) = +R(0, 1) * Dinv(0, 0) * R(0, 1) - R(0, 1) * Dinv(0, 1) * R(0, 0) - R(0, 0) * Dinv(1, 0) * R(0, 1) + R(0, 0) * Dinv(1, 1) * R(0, 0);
        (*dRdF)(6, 7) = +R(0, 1) * Dinv(0, 0) * R(1, 1) - R(0, 1) * Dinv(0, 1) * R(1, 0) - R(0, 0) * Dinv(1, 0) * R(1, 1) + R(0, 0) * Dinv(1, 1) * R(1, 0);
        (*dRdF)(6, 8) = +R(0, 1) * Dinv(0, 0) * R(2, 1) - R(0, 1) * Dinv(0, 1) * R(2, 0) - R(0, 0) * Dinv(1, 0) * R(2, 1) + R(0, 0) * Dinv(1, 1) * R(2, 0);
        (*dRdF)(7, 0) = +R(1, 1) * Dinv(0, 1) * R(0, 2) - R(1, 1) * Dinv(0, 2) * R(0, 1) - R(1, 0) * Dinv(1, 1) * R(0, 2) + R(1, 0) * Dinv(1, 2) * R(0, 1);
        (*dRdF)(7, 1) = +R(1, 1) * Dinv(0, 1) * R(1, 2) - R(1, 1) * Dinv(0, 2) * R(1, 1) - R(1, 0) * Dinv(1, 1) * R(1, 2) + R(1, 0) * Dinv(1, 2) * R(1, 1);
        (*dRdF)(7, 2) = +R(1, 1) * Dinv(0, 1) * R(2, 2) - R(1, 1) * Dinv(0, 2) * R(2, 1) - R(1, 0) * Dinv(1, 1) * R(2, 2) + R(1, 0) * Dinv(1, 2) * R(2, 1);
        (*dRdF)(7, 3) = -R(1, 1) * Dinv(0, 0) * R(0, 2) + R(1, 1) * Dinv(0, 2) * R(0, 0) + R(1, 0) * Dinv(1, 0) * R(0, 2) - R(1, 0) * Dinv(1, 2) * R(0, 0);
        (*dRdF)(7, 4) = -R(1, 1) * Dinv(0, 0) * R(1, 2) + R(1, 1) * Dinv(0, 2) * R(1, 0) + R(1, 0) * Dinv(1, 0) * R(1, 2) - R(1, 0) * Dinv(1, 2) * R(1, 0);
        (*dRdF)(7, 5) = -R(1, 1) * Dinv(0, 0) * R(2, 2) + R(1, 1) * Dinv(0, 2) * R(2, 0) + R(1, 0) * Dinv(1, 0) * R(2, 2) - R(1, 0) * Dinv(1, 2) * R(2, 0);
        (*dRdF)(7, 6) = +R(1, 1) * Dinv(0, 0) * R(0, 1) - R(1, 1) * Dinv(0, 1) * R(0, 0) - R(1, 0) * Dinv(1, 0) * R(0, 1) + R(1, 0) * Dinv(1, 1) * R(0, 0);
        (*dRdF)(7, 7) = +R(1, 1) * Dinv(0, 0) * R(1, 1) - R(1, 1) * Dinv(0, 1) * R(1, 0) - R(1, 0) * Dinv(1, 0) * R(1, 1) + R(1, 0) * Dinv(1, 1) * R(1, 0);
        (*dRdF)(7, 8) = +R(1, 1) * Dinv(0, 0) * R(2, 1) - R(1, 1) * Dinv(0, 1) * R(2, 0) - R(1, 0) * Dinv(1, 0) * R(2, 1) + R(1, 0) * Dinv(1, 1) * R(2, 0);
        (*dRdF)(8, 0) = +R(2, 1) * Dinv(0, 1) * R(0, 2) - R(2, 1) * Dinv(0, 2) * R(0, 1) - R(2, 0) * Dinv(1, 1) * R(0, 2) + R(2, 0) * Dinv(1, 2) * R(0, 1);
        (*dRdF)(8, 1) = +R(2, 1) * Dinv(0, 1) * R(1, 2) - R(2, 1) * Dinv(0, 2) * R(1, 1) - R(2, 0) * Dinv(1, 1) * R(1, 2) + R(2, 0) * Dinv(1, 2) * R(1, 1);
        (*dRdF)(8, 2) = +R(2, 1) * Dinv(0, 1) * R(2, 2) - R(2, 1) * Dinv(0, 2) * R(2, 1) - R(2, 0) * Dinv(1, 1) * R(2, 2) + R(2, 0) * Dinv(1, 2) * R(2, 1);
        (*dRdF)(8, 3) = -R(2, 1) * Dinv(0, 0) * R(0, 2) + R(2, 1) * Dinv(0, 2) * R(0, 0) + R(2, 0) * Dinv(1, 0) * R(0, 2) - R(2, 0) * Dinv(1, 2) * R(0, 0);
        (*dRdF)(8, 4) = -R(2, 1) * Dinv(0, 0) * R(1, 2) + R(2, 1) * Dinv(0, 2) * R(1, 0) + R(2, 0) * Dinv(1, 0) * R(1, 2) - R(2, 0) * Dinv(1, 2) * R(1, 0);
        (*dRdF)(8, 5) = -R(2, 1) * Dinv(0, 0) * R(2, 2) + R(2, 1) * Dinv(0, 2) * R(2, 0) + R(2, 0) * Dinv(1, 0) * R(2, 2) - R(2, 0) * Dinv(1, 2) * R(2, 0);
        (*dRdF)(8, 6) = +R(2, 1) * Dinv(0, 0) * R(0, 1) - R(2, 1) * Dinv(0, 1) * R(0, 0) - R(2, 0) * Dinv(1, 0) * R(0, 1) + R(2, 0) * Dinv(1, 1) * R(0, 0);
        (*dRdF)(8, 7) = +R(2, 1) * Dinv(0, 0) * R(1, 1) - R(2, 1) * Dinv(0, 1) * R(1, 0) - R(2, 0) * Dinv(1, 0) * R(1, 1) + R(2, 0) * Dinv(1, 1) * R(1, 0);
        (*dRdF)(8, 8) = +R(2, 1) * Dinv(0, 0) * R(2, 1) - R(2, 1) * Dinv(0, 1) * R(2, 0) - R(2, 0) * Dinv(1, 0) * R(2, 1) + R(2, 0) * Dinv(1, 1) * R(2, 0);
    }
}

template void FtoR(const Eigen::Matrix<float, 3, 3>& F, Eigen::Matrix<float, 3, 3>& R, Eigen::Matrix<float, 9, 9>* dRdF);
template void FtoR(const Eigen::Matrix<double, 3, 3>& F, Eigen::Matrix<double, 3, 3>& R, Eigen::Matrix<double, 9, 9>* dRdF);

template <class T>
void TetConstraints<T>::SetRestPose(const Eigen::Matrix<T, 3, -1>& vertices, bool allowInvertedTets)
{
    const int numTets = int(m_tets.cols());
    m_numVertices = int(vertices.cols());

    m_invRestFrame.resize(size_t(numTets));
    m_sqrtRestVolume.resize(size_t(numTets));

    for (int t = 0; t < numTets; t++)
    {
        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Vector3<T>& v0 = vertices.col(v[0]);
        const Eigen::Vector3<T>& v1 = vertices.col(v[1]);
        const Eigen::Vector3<T>& v2 = vertices.col(v[2]);
        const Eigen::Vector3<T>& v3 = vertices.col(v[3]);

        Eigen::Matrix<T, 3, 3> restFrame;
        restFrame.col(0) = v1 - v0;
        restFrame.col(1) = v2 - v0;
        restFrame.col(2) = v3 - v0;

        const T restDet = restFrame.determinant();
        if (!allowInvertedTets && (restDet < T(1e-9))) { CARBON_CRITICAL("Tet with tiny or even negative volume in the rest pose"); }
        m_sqrtRestVolume[t] = sqrt(abs(restDet) / T(6.0));
        if (m_sqrtRestVolume[t] > 1e-12)
        {
            m_invRestFrame[t] = restFrame.inverse();
        }
        else
        {
            m_invRestFrame[t].setZero();
        }
    }
}

template <class T>
void TetConstraints<T>::SetTetsMask(const Eigen::Vector<int, -1>& mask)
{
    const int numTets = int(m_tets.cols());
    if(static_cast<int>(mask.size()) != numTets)
    {
        CARBON_CRITICAL("The mask should have one entry for each tet.");
    }
    m_mask = mask;
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateStrainCorotated(const DiffDataMatrix<T, 3, -1>& vertices, const T strainWeight) const
{
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    Vector<T> outputValue(numTets * 9);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    const T strainWeightSqrt = sqrt(strainWeight);

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;
        
        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);
        Eigen::Matrix<T, 9, 9> dRdF;
        Eigen::Matrix<T, 3, 3> R;
        FtoR(F, R, vertices.HasJacobian() ? &dRdF : nullptr);

        const T coefficient = strainWeightSqrt * m_sqrtRestVolume[t];
        Eigen::Map<Eigen::Matrix<T, 3, 3>>(outputValue.segment(9 * t, 9).data()) = coefficient * (F - R);

        if (vertices.HasJacobian())
        {
            Eigen::Matrix<T, 9, 9> dResdF = Eigen::Matrix<T, 9, 9>::Identity() - dRdF;

            for (int i = 0; i < 3; i++) // dv1, dv2, dv3
            {
                Eigen::Matrix<T, 9, 3> dFdx = Eigen::Matrix<T, 9, 3>::Zero();
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        dFdx(3 * j + c, c) = coefficient * m_invRestFrame[t](i, j);
                    }
                }
                const Eigen::Matrix<T, 9, 3> dResdx = dResdF * dFdx;
                for (int k = 0; k < 9; k++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(9 * t + k, 3 * v[i + 1] + c, dResdx(k, c)));
                    }
                }
            }
            // dv0 is special:
            {
                const Eigen::Vector<T, 3> sum = Eigen::Matrix<T, 1, 3>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
                Eigen::Matrix<T, 9, 3> dFdx = Eigen::Matrix<T, 9, 3>::Zero();
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        dFdx(3 * j + c, c) = sum[j];
                    }
                }
                const Eigen::Matrix<T, 9, 3> dResdx = dResdF * dFdx;
                for (int k = 0; k < 9; k++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(9 * t + k, 3 * v[0] + c, dResdx(k, c)));
                    }
                }
            }
        }
    }

    JacobianConstPtr<T> Jacobian;

    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(int(outputValue.size()), vertices.Size());
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffData<T>(std::move(outputValue), Jacobian);
}

template <class T>
void TetConstraints<T>::SetupStrain(const Eigen::Matrix<T, 3, -1>& vertices, const T strainWeight, VertexConstraintsExt<T, 9, 4>& vertexConstraints) const
{
    const int numTets = (int)m_tets.cols();
    if ((int)vertices.cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if ((int)m_invRestFrame.size() != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    vertexConstraints.ResizeToFitAdditionalConstraints(numTets);
    const T strainWeightSqrt = sqrt(strainWeight);

    Eigen::Matrix<T, 9, 9> dRdF;
    Eigen::Matrix<T, 3, 3> R;
    Eigen::Matrix<T, 3, 3> currFrame;
    Eigen::Vector<T, 9> residual;
    Eigen::Matrix<T, 9, 3 * 4> jac;
    Eigen::Matrix<T, 3, 3> F;
    Eigen::Matrix<T, 9, 9> dResdF;

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

        const auto tet = m_tets.col(t);
        const auto v0 = vertices.col(tet[0]);
        const auto v1 = vertices.col(tet[1]);
        const auto v2 = vertices.col(tet[2]);
        const auto v3 = vertices.col(tet[3]);

        currFrame.col(0).noalias() = v1 - v0;
        currFrame.col(1).noalias() = v2 - v0;
        currFrame.col(2).noalias() = v3 - v0;

        F.noalias() = currFrame * m_invRestFrame[t];
        FtoR(F, R, &dRdF);

        const T coefficient = strainWeightSqrt * m_sqrtRestVolume[t];
        Eigen::Map<Eigen::Matrix<T, 3, 3>>(residual.data()).noalias() = coefficient * (F - R);
        dResdF.noalias() = Eigen::Matrix<T, 9, 9>::Identity() - dRdF;

        jac.setZero();

        const Eigen::RowVector3<T> sum = Eigen::RowVector3<T>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
        for (int j = 0; j < 3; ++j)
        {
            for (int k = 0; k < 3; ++k)
            {
                jac.col(3 * 0 + k) += dResdF.col(3 * j + k) * sum[j];
            }
        }
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                for (int k = 0; k < 3; ++k)
                {
                    jac.col(3 * (i + 1) + k).noalias() += dResdF.col(3 * j + k) * coefficient * m_invRestFrame[t](i, j);
                }
            }
        }
        vertexConstraints.AddConstraint(tet, residual, jac);
    }
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateStrainLinearProjective(
    const DiffDataMatrix<T, 3, -1>& vertices, 
    const T strainWeight, 
    ElasticityModel elModel,
    T minRange,
    T maxRange) const
{
    if (validElModels.find(elModel) == validElModels.end() ) 
        { CARBON_CRITICAL("Incorrect elasticity model"); } 

    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    Vector<T> outputValue(numTets * 9);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    const T strainWeightSqrt = sqrt(strainWeight);

    for (int t = 0; t < numTets; t++)
    {
        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);
        Eigen::Matrix<T, 3, 3> Fdash;
        if (elModel == Corotated)
        {
            if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

            const Eigen::JacobiSVD<Eigen::Matrix<T, 3, 3>> svd(F, Eigen::ComputeFullU | Eigen::ComputeFullV);
            Eigen::Vector3<T> S;
            if ((minRange != T(1)) || (maxRange != T(1)))
            {
                S = svd.singularValues();
                S[0] = std::clamp(S[0], minRange, maxRange);
                S[1] = std::clamp(S[1], minRange, maxRange);
                S[2] = std::clamp(S[2], minRange, maxRange);
            }
            else
            {
                S = Eigen::Vector3<T>::Ones();
            }
            
            if (F.determinant() < 0)
            {
                // F is a reflection, so we need to invert the matrix
                S[2] = -S[2];
            }

            Fdash = svd.matrixU() * S.asDiagonal() * svd.matrixV().transpose();
        }
        else if (elModel == Linear)
        {
            Fdash = Eigen::Matrix<T, 3, 3>::Identity();
        }
        else
        {
            CARBON_CRITICAL("The provided material model is defined only for the projective strain");
        }

        const T coefficient = strainWeightSqrt * m_sqrtRestVolume[t];
        Eigen::Map<Eigen::Matrix<T, 3, 3>>(outputValue.segment(9 * t, 9).data()) = coefficient * (F - Fdash);

        if (vertices.HasJacobian())
        {
            for (int i = 0; i < 3; i++) // dv1, dv2, dv3
            {
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(9 * t + 3 * j + c, 3 * v[i + 1] + c, coefficient * m_invRestFrame[t](i, j)));
                    }
                }
            }
            // dv0 is special:
            const Eigen::Vector<T, 3> sum = Eigen::Matrix<T, 1, 3>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
            for (int j = 0; j < 3; j++) // x, y, z
            {
                for (int c = 0; c < 3; c++)
                {
                    triplets.push_back(Eigen::Triplet<T>(9 * t + 3 * j + c, 3 * v[0] + c, sum[j]));
                }
            }
        }
    }

    JacobianConstPtr<T> Jacobian;

    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(int(outputValue.size()), vertices.Size());
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffData<T>(std::move(outputValue), Jacobian);
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateVolumeLoss(const DiffDataMatrix<T, 3, -1>& vertices, T volumeWeight) const
{
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    Vector<T> outputValue(numTets);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    const T volumeWeightSqrt = sqrt(volumeWeight);

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);
        const T V = F.determinant();

        const T coefficient = volumeWeightSqrt * m_sqrtRestVolume[t];
        outputValue[t] = volumeWeightSqrt * m_sqrtRestVolume[t] * (V - T(1));

        if (vertices.HasJacobian())
        {
            Eigen::Matrix<T, 3, 3> dVdF;
            dVdF(0, 0) = F(1, 1) * F(2, 2) - F(2, 1) * F(1, 2);
            dVdF(0, 1) = F(2, 0) * F(1, 2) - F(1, 0) * F(2, 2);
            dVdF(0, 2) = F(1, 0) * F(2, 1) - F(2, 0) * F(1, 1);
            dVdF(1, 0) = F(2, 1) * F(0, 2) - F(0, 1) * F(2, 2);
            dVdF(1, 1) = F(0, 0) * F(2, 2) - F(2, 0) * F(0, 2);
            dVdF(1, 2) = F(2, 0) * F(0, 1) - F(0, 0) * F(2, 1);
            dVdF(2, 0) = F(0, 1) * F(1, 2) - F(1, 1) * F(0, 2);
            dVdF(2, 1) = F(1, 0) * F(0, 2) - F(0, 0) * F(1, 2);
            dVdF(2, 2) = F(0, 0) * F(1, 1) - F(1, 0) * F(0, 1);

            for (int i = 0; i < 3; i++) // dv1, dv2, dv3
            {
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(t, 3 * v[i + 1] + c, dVdF(c, j) * coefficient * m_invRestFrame[t](i, j)));
                    }
                }
            }
            // dv0 is special:
            const Eigen::Vector<T, 3> sum = Eigen::Matrix<T, 1, 3>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
            for (int j = 0; j < 3; j++) // x, y, z
            {
                for (int c = 0; c < 3; c++)
                {
                    triplets.push_back(Eigen::Triplet<T>(t, 3 * v[0] + c, dVdF(c, j) * sum[j]));
                }
            }
        }
    }

    JacobianConstPtr<T> Jacobian;

    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(int(outputValue.size()), vertices.Size());
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffData<T>(std::move(outputValue), Jacobian);
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateVolumeLossProjective(
    const DiffDataMatrix<T, 3, -1>& vertices, 
    T volumeWeight,
    T minRange,
    T maxRange) const
{
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    Vector<T> outputValue(numTets * 9);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    const T volumeWeightSqrt = sqrt(volumeWeight);

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);
        const Eigen::JacobiSVD<Eigen::Matrix<T, 3, 3>> svd(F, Eigen::ComputeFullU | Eigen::ComputeFullV);
        Eigen::Vector3<T> S = svd.singularValues();

        constexpr int innerIterations = 4;
        Eigen::Vector3<T> d = Eigen::Vector3<T>::Zero();
        for (int i = 0; i < innerIterations; ++i)
        {
            const T currentVolume = S[0] * S[1] * S[2];
            // const T f = currentVolume - T(1); // clamp(v, rangeMin_, rangeMax_);
            T f = T(0);
            if((minRange != T(0)) || (maxRange != T(0)))
            {
                // if(currentVolume < minRange) f = currentVolume - minRange;
                // if(currentVolume > maxRange) f = currentVolume - maxRange;
                f = currentVolume - clamp(currentVolume, minRange, maxRange);
            }
            else
            {
                f = currentVolume - T(1);
            }
            Eigen::Vector3<T> g(S[1] * S[2], S[0] * S[2], S[0] * S[1]);
            d = -((f - g.dot(d)) / g.dot(g)) * g;
            S = svd.singularValues() + d;
        }
        if (F.determinant() < 0)
        {
            // F is a reflection, so we need to invert the matrix
            S[2] = -S[2];
        }
        const Eigen::Matrix<T, 3, 3> Fdash = svd.matrixU() * S.asDiagonal() * svd.matrixV().transpose();

        const T coefficient = volumeWeightSqrt * m_sqrtRestVolume[t];
        Eigen::Map<Eigen::Matrix<T, 3, 3>>(outputValue.segment(9 * t, 9).data()) = coefficient * (F - Fdash);

        if (vertices.HasJacobian())
        {
            for (int i = 0; i < 3; i++) // dv1, dv2, dv3
            {
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(9 * t + 3 * j + c, 3 * v[i + 1] + c, coefficient * m_invRestFrame[t](i, j)));
                    }
                }
            }
            // dv0 is special:
            const Eigen::Vector<T, 3> sum = Eigen::Matrix<T, 1, 3>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
            for (int j = 0; j < 3; j++) // x, y, z
            {
                for (int c = 0; c < 3; c++)
                {
                    triplets.push_back(Eigen::Triplet<T>(9 * t + 3 * j + c, 3 * v[0] + c, sum[j]));
                }
            }
        }
    }

    JacobianConstPtr<T> Jacobian;

    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(int(outputValue.size()), vertices.Size());
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffData<T>(std::move(outputValue), Jacobian);
}

template <class T>
DiffDataMatrix<T, 9, -1> TetConstraints<T>::EvaluateDeformationGradient(const DiffDataMatrix<T, 3, -1>& vertices,
                                                                        bool volumeWeighted,
                                                                        const std::vector<T>& perTetWeight) const
{
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    Vector<T> outputValue(numTets * 9);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);

        T coefficient = volumeWeighted ? m_sqrtRestVolume[t] : T(1);
        if (static_cast<int>(perTetWeight.size()) == numTets)
        {
            coefficient *= perTetWeight[t];
        }
        Eigen::Map<Eigen::Matrix<T, 3, 3>>(outputValue.segment(9 * t, 9).data()) = coefficient * F;

        if (vertices.HasJacobian())
        {
            for (int i = 0; i < 3; i++) // dv1, dv2, dv3
            {
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(9 * t + 3 * j + c, 3 * v[i + 1] + c, coefficient * m_invRestFrame[t](i, j)));
                    }
                }
            }
            // dv0 is special:
            const Eigen::Vector<T, 3> sum = Eigen::Matrix<T, 1, 3>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
            for (int j = 0; j < 3; j++) // x, y, z
            {
                for (int c = 0; c < 3; c++)
                {
                    triplets.push_back(Eigen::Triplet<T>(9 * t + 3 * j + c, 3 * v[0] + c, sum[j]));
                }
            }
        }
    }

    JacobianConstPtr<T> Jacobian;

    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(int(outputValue.size()), vertices.Size());
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffDataMatrix<T, 9, -1>(9, numTets, DiffData<T>(std::move(outputValue), Jacobian));
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateStrain(const DiffDataMatrix<T, 3, -1>& vertices, const T strainWeight, ElasticityModel elModel) const
{
    if (validElModels.find(elModel) == validElModels.end()) 
        { CARBON_CRITICAL("Incorrect elasticity model"); } 
   
    if (elModel == NeoHookean)
    {
        return EvaluateStrainNH(vertices, strainWeight);
    }
    else if (elModel == Corotated)
    {
        return EvaluateStrainCorotated(vertices, strainWeight);
    }  
    else
    {
        CARBON_CRITICAL("The provided material model is defined only for the projective strain");
    }
    // return EvaluateStrainCorotated(vertices, strainWeight);
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateStrainNH(const DiffDataMatrix<T, 3, -1>& vertices, const T strainWeight) const
{
     
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    Vector<T> outputValue(numTets);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    const T strainWeightSqrt = sqrt(strainWeight);

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);
        T trC = F.array().square().sum(); // Frobenius norm
        T sqrtTrC = std::sqrt(trC);
        T invSqrtTrC = T(1) / sqrtTrC;

        const T coefficient = strainWeightSqrt * m_sqrtRestVolume[t];
        outputValue[t] = coefficient * (sqrtTrC - std::sqrt(T(3)));

        if (vertices.HasJacobian())
        {
            Eigen::Vector<T, 9> dtrCdF;
            for(int i = 0; i < 3; i++){
                for(int j = 0; j < 3; j++){
                    dtrCdF(i * 3 + j) = invSqrtTrC * F(j, i);
                }
            }

            for (int i = 0; i < 3; i++) // dv1, dv2, dv3
            {
                Eigen::Matrix<T, 9, 3> dFdx = Eigen::Matrix<T, 9, 3>::Zero();
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        dFdx(3 * j + c, c) = coefficient * m_invRestFrame[t](i, j);
                    }
                }
                const Eigen::Vector<T, 3> dWdx = dFdx.transpose() * dtrCdF;
                for (int c = 0; c < 3; c++)
                {
                    triplets.push_back(Eigen::Triplet<T>(t, 3 * v[i + 1] + c, dWdx(c)));
                }
            }
            // dv0 is special:
            {
                const Eigen::Vector<T, 3> sum = Eigen::Matrix<T, 1, 3>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
                Eigen::Matrix<T, 9, 3> dFdx = Eigen::Matrix<T, 9, 3>::Zero();
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        dFdx(3 * j + c, c) = sum[j];
                    }
                }
                const Eigen::Vector<T, 3> dWdx = dFdx.transpose() * dtrCdF;
                for (int c = 0; c < 3; c++)
                {
                    triplets.push_back(Eigen::Triplet<T>(t, 3 * v[0] + c, dWdx(c)));
                }
            }
        }
    }

    JacobianConstPtr<T> Jacobian;

    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(int(outputValue.size()), vertices.Size());
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffData<T>(std::move(outputValue), Jacobian);
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateStrainActivation(
    const DiffDataMatrix<T, 3, -1>& vertices, 
    const Eigen::Matrix<T, 9, -1>& activations,
    const T strainWeight) const
{
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }
    if (int(activations.cols()) != numTets) { CARBON_CRITICAL("Incorrect number of activations"); }

    Vector<T> outputValue(numTets * 9);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    const T strainWeightSqrt = sqrt(strainWeight);

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);
        Eigen::Matrix<T, 9, 9> dRdF;
        Eigen::Matrix<T, 3, 3> R;
        FtoR(F, R, vertices.HasJacobian() ? &dRdF : nullptr);

        Eigen::Vector<T, 9> AVector = activations.col(t);
        const Eigen::Matrix<T, 3, 3> A = Eigen::Map<Eigen::Matrix<T, 3, 3>>(AVector.data());

        const T coefficient = strainWeightSqrt * m_sqrtRestVolume[t];
        Eigen::Map<Eigen::Matrix<T, 3, 3>>(outputValue.segment(9 * t, 9).data()) = coefficient * (F - R * A);

        if (vertices.HasJacobian())
        {
            for (int i = 0; i < 3; i++) // dv1, dv2, dv3
            {
                Eigen::Matrix<T, 9, 3> dFdx = Eigen::Matrix<T, 9, 3>::Zero();
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        dFdx(3 * j + c, c) = coefficient * m_invRestFrame[t](i, j);
                    }
                }
                Eigen::Matrix<T, 9, 3> dRdx = dRdF * dFdx;
                Eigen::Matrix<T, 9, 3> dRdx_A;
                for(int m = 0; m < 3; m++){
                    const Eigen::Matrix<T, 3, 3>& dRdx_m = Eigen::Map<Eigen::Matrix<T, 3, 3>>(dRdx.col(m).data());
                    Eigen::Map<Eigen::Matrix<T, 3, 3>>(dRdx_A.col(m).data()) = dRdx_m * A;
                }
                Eigen::Matrix<T, 9, 3> dResdx = dFdx - dRdx_A;
                for (int k = 0; k < 9; k++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(9 * t + k, 3 * v[i + 1] + c, dResdx(k, c)));
                    }
                }
            }
            // dv0 is special:
            {
                const Eigen::Vector<T, 3> sum = Eigen::Matrix<T, 1, 3>(T(-1.0), T(-1.0), T(-1.0)) * coefficient * m_invRestFrame[t];
                Eigen::Matrix<T, 9, 3> dFdx = Eigen::Matrix<T, 9, 3>::Zero();
                for (int j = 0; j < 3; j++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        dFdx(3 * j + c, c) = sum[j];
                    }
                }
                Eigen::Matrix<T, 9, 3> dRdx = dRdF * dFdx;
                Eigen::Matrix<T, 9, 3> dRdx_A;
                for(int m = 0; m < 3; m++){
                    const Eigen::Matrix<T, 3, 3>& dRdx_m = Eigen::Map<Eigen::Matrix<T, 3, 3>>(dRdx.col(m).data());
                    Eigen::Map<Eigen::Matrix<T, 3, 3>>(dRdx_A.col(m).data()) = dRdx_m * A;
                }
                Eigen::Matrix<T, 9, 3> dResdx = dFdx - dRdx_A;
                for (int k = 0; k < 9; k++) // x, y, z
                {
                    for (int c = 0; c < 3; c++)
                    {
                        triplets.push_back(Eigen::Triplet<T>(9 * t + k, 3 * v[0] + c, dResdx(k, c)));
                    }
                }
            }
        }
    }

    JacobianConstPtr<T> Jacobian;

    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(int(outputValue.size()), vertices.Size());
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffData<T>(std::move(outputValue), Jacobian);
}

template <class T>
DiffDataMatrix<T, 9, -1> TetConstraints<T>::EvaluateDeformationGradientLossProjective(
    const DiffDataMatrix<T, 3, -1>& vertices, 
    const Eigen::Matrix<T, 9, -1>& targetGradients,
    bool volumeWeighted) const
{
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }
    if (int(targetGradients.cols()) != numTets) { CARBON_CRITICAL("Incorrect number of target deformation gradients"); }
    
    const DiffDataMatrix<T, 9, -1> currentGradientsDiffData = EvaluateDeformationGradient(vertices, volumeWeighted);
    const Eigen::Matrix<T, 9, -1> currentGradients = currentGradientsDiffData.Matrix();
    Eigen::Matrix<T, 9, -1> modifiedTargetGradients = targetGradients;

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;

        Eigen::Matrix<T, 3, 3> FTarget, FCurr; 
        Eigen::Map<Eigen::Vector<T, 9>>(FTarget.data()) = targetGradients.col(t);
        Eigen::Map<Eigen::Vector<T, 9>>(FCurr.data()) = currentGradients.col(t);

        if ( (FCurr.determinant() < 0) )
        {
            const Eigen::JacobiSVD<Eigen::Matrix<T, 3, 3>> svd(FTarget, Eigen::ComputeFullU | Eigen::ComputeFullV);
            Eigen::Vector3<T> S = svd.singularValues();
            S[2] = -S[2];

            Eigen::Map<Eigen::Matrix<T, 3, 3>>(modifiedTargetGradients.col(t).data()) = svd.matrixU() * S.asDiagonal() * svd.matrixV().transpose();
        }
    }

    DiffDataMatrix<T, 9, -1> modifiedTargetGradientsDiffData(modifiedTargetGradients);
    return currentGradientsDiffData - modifiedTargetGradientsDiffData;
}

template <class T>
DiffData<T> TetConstraints<T>::EvaluateGravityPotential(
    const DiffDataMatrix<T, 3, -1>& vertices, 
    const T acceleration,
    const T density,
    const int hAxis) const
{
    // Note: there is a discontinuity in location[hAxis] = 0 (zero height).
    if (hAxis < 0 || hAxis >= 3)
    {
        CARBON_CRITICAL("The height axis {} is out of bounds", hAxis);
    }

    const int NumPoints = static_cast<int>(vertices.Cols());
    const int NumTets = static_cast<int>(m_tets.cols());

    Eigen::Vector<T, -1> output = Eigen::Vector<T, -1>::Zero(NumPoints);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) 
    {
        triplets.reserve(4 * NumTets + NumPoints); // 4 entries per tet (potential) plus 1 entry per point (barriers)
    }

    const T sqrtDensity = std::sqrt(density);
    const T sqrtAcceleration = std::sqrt(std::abs(acceleration));
    for (int i = 0; i < NumTets; ++i)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[i] == 0)) continue;

        const T sqrtTetForce = m_sqrtRestVolume[i] * sqrtDensity * sqrtAcceleration;

        for (int k = 0; k < 4; ++k)
        {   
            const T signedHeight = vertices.Matrix()(hAxis, m_tets(k, i));
            if (signedHeight > 0)
            {
                const T sqrtHeight = std::sqrt(signedHeight);
                output[m_tets(k, i)] += T(0.25) * sqrtTetForce * sqrtHeight; //  m_sqrtRestVolume[i] * 

                if (vertices.HasJacobian()) 
                {
                    const T f = T(0.125) /*T(0.5) * T(0.25)*/ * sqrtTetForce * (T(1) / sqrtHeight); // m_sqrtRestVolume[i] * 
                    triplets.push_back(Eigen::Triplet<T>(m_tets(k, i), m_tets(k, i) * 3 + 1, f));
                } 
            }
            // else if (signedHeight > 0)
            // {
            //     output[m_tets(k, i)] += T(0);
            // }
        }
    }

    JacobianConstPtr<T> Jacobian;
    if (vertices.HasJacobian())
    {
        SparseMatrix<T> localJacobian(NumPoints, 3 * NumPoints);
        localJacobian.setFromTriplets(triplets.begin(), triplets.end());
        Jacobian = vertices.Jacobian().Premultiply(localJacobian);
    }

    return DiffData<T>(std::move(output), Jacobian);
}

template <class T>
Eigen::VectorX<T> TetConstraints<T>::EvaluateCauchyGreenStrainTensor(
    const DiffDataMatrix<T, 3, -1>& vertices) const
{
    const int numTets = int(m_tets.cols());
    if (vertices.Cols() != m_numVertices) { CARBON_CRITICAL("Incorrect number of vertices"); }
    if (int(m_invRestFrame.size()) != numTets) { CARBON_CRITICAL("Incorrect number of tets"); }

    Vector<T> outputValue(numTets * 9);
    std::vector<Eigen::Triplet<T>> triplets;
    if (vertices.HasJacobian()) { triplets.reserve(size_t(numTets) * 36); }

    for (int t = 0; t < numTets; t++)
    {
        if ((static_cast<int>(m_mask.size()) > 0) && (m_mask[t] == 0)) continue;
        
        const int v[4] = { m_tets(0, t), m_tets(1, t), m_tets(2, t), m_tets(3, t) };
        const Eigen::Matrix<T, 3, 3> F = EvaluateDeformationGradient(vertices, v, t);
        const T coefficient = m_sqrtRestVolume[t];
        Eigen::Map<Eigen::Matrix<T, 3, 3>>(outputValue.segment(9 * t, 9).data()) = 
            coefficient * T(0.5) * (F.transpose() * F - Eigen::Matrix<T, 3, 3>::Identity(3, 3));
    }

    return outputValue; 
}

template <class T>
Eigen::Matrix<T, 3, 3> TetConstraints<T>::EvaluateDeformationGradient(const DiffDataMatrix<T, 3, -1>& vertices, const int* v, const int t) const
{
    const Eigen::Vector3<T>& v0 = vertices.Matrix().col(v[0]);
    const Eigen::Vector3<T>& v1 = vertices.Matrix().col(v[1]);
    const Eigen::Vector3<T>& v2 = vertices.Matrix().col(v[2]);
    const Eigen::Vector3<T>& v3 = vertices.Matrix().col(v[3]);

    Eigen::Matrix<T, 3, 3> currFrame;
    currFrame.col(0) = v1 - v0;
    currFrame.col(1) = v2 - v0;
    currFrame.col(2) = v3 - v0;

    return currFrame * m_invRestFrame[t];
}

template class TetConstraints<float>;
template class TetConstraints<double>;

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
