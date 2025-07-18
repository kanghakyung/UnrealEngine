// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>
#include <carbon/utils/TaskThreadPool.h>
#include <nls/geometry/VertexConstraints.h>
#include <nls/geometry/Mesh.h>

#include <nrr/DepthmapConstraints.h>
#include <nrr/FlowConstraints.h>
#include <nrr/ICPConstraints.h>
#include <nrr/ImageConstraints.h>
#include <nrr/LipClosureConstraints.h>
#include <nrr/landmarks/LandmarkConstraints2D.h>
#include <nrr/rt/PCARig.h>

#include <memory>
#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

class PCAFaceFitting
{
public:
    struct Settings
    {
        bool withRigid = false;
        int iterations = 5;
        float pcaRegularization = 0.1f;
        float pcaVelocityRegularization = 0.0f;
        float pcaAccelerationRegularization = 0.0f;
    };

    //! The Cache keeps the memory allocated for the various vertex constraints as well as the jacobians that are calculated at each solver iteration.
    struct Cache
    {
        VertexConstraints<float, 1, 1> point2SurfaceVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> point2SurfaceVertexConstraintsJacobian;
        VertexConstraints<float, 3, 1> point2PointVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> point2PointVertexConstraintsJacobian;
        VertexConstraints<float, 2, 3> landmarksVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> landmarksVertexConstraintsJacobian;
        VertexConstraints<float, 1, 3> curvesVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> curvesVertexConstraintsJacobian;
        VertexConstraints<float, 1, 2> contourVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> contourVertexConstraintsJacobian;
        VertexConstraints<float, 1, 3> eyeLeftCurvesVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> eyeLeftCurvesVertexConstraintsJacobian;
        VertexConstraints<float, 1, 3> eyeRightCurvesVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> eyeRightCurvesVertexConstraintsJacobian;
        VertexConstraints<float, 2, 3> teethVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> teethVertexConstraintsJacobian;
        VertexConstraints<float, 2, 1> flowVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> flowVertexConstraintsJacobian;
        VertexConstraints<float, 1, 1> imageVertexConstraints;
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> imageVertexConstraintsJacobian;
        // VertexConstraints<float, 3, 1> lipClosureVertexConstraints; //!< lip closure against mid point
        VertexConstraints<float, 3, 4> lipClosureVertexConstraints; //!< lip closure against opposite lip
        Eigen::Matrix<float, -1, -1, Eigen::RowMajor> lipClosureVertexConstraintsJacobian;

        void Clear()
        {
            point2SurfaceVertexConstraints.Clear();
            point2PointVertexConstraints.Clear();
            landmarksVertexConstraints.Clear();
            curvesVertexConstraints.Clear();
            contourVertexConstraints.Clear();
            eyeLeftCurvesVertexConstraints.Clear();
            eyeRightCurvesVertexConstraints.Clear();
            teethVertexConstraints.Clear();
            flowVertexConstraints.Clear();
            imageVertexConstraints.Clear();
            lipClosureVertexConstraints.Clear();
        }
    };


    // tracking state
    struct State
    {
        rt::LinearVertexModel<float> face;
        rt::LinearVertexModel<float> neck;
        rt::LinearVertexModel<float> teeth;
        rt::LinearVertexModel<float> eyeLeft;
        rt::LinearVertexModel<float> eyeRight;
        Cache cache;
    };

public:
    ~PCAFaceFitting();
    PCAFaceFitting();

    /**
     * Load the PCA rig from (DNA) file pcaFilename
     * @pre must have loaded the tracking rig before loading the PCA rig
     */
    bool LoadPCARig(const std::string& pcaFilename);

    /**
     * Load the PCA rig from (DNA) Reader dnaStream
     * @pre must have loaded the tracking rig before loading the PCA rig
     */
    bool LoadPCARig(dna::Reader* dnaStream);

    bool SavePCARig(dna::Writer* dnaStream) const;
    void SavePCARigAsNpy(const std::string& filename) const;

    //! @return the loaded pca rig
    const rt::PCARig& GetPCARig() const { return m_pcaRig; }

    //! @return the alignment of the pca rig to the original mesh
    Affine<float, 3, 3> PCARigToMesh() const { return m_pcaRig.ToOriginalPosition(); }

    //! Fits the PCA model.
    void FitPCAData(const Mesh<float>& topology,
                    std::vector<DepthmapConstraints>& vectorOfDepthmapConstraints,
                    ICPConstraints<float>* icpConstraints,
                    LandmarkConstraints2D<float>* landmarkConstraints,
                    const std::vector<FlowConstraints<float>*>& vectorOfFlowConstraints,
                    ImageConstraints<float>* imageConstraints,
                    LipClosureConstraints<float>* lipClosureConstraints,
                    Affine<float, 3, 3>& rigidMotion,
                    Eigen::VectorX<float>& pcaCoeffs,
                    const std::vector<Eigen::VectorX<float>>& pcaCoeffsPrevFrames,
                    const Settings& settings,
                    std::vector<State>& states) const;

    //! Fits the PCA model for neck.
    void FitPCADataNeck(const Mesh<float>& topology,
                        std::vector<DepthmapConstraints>& vectorOfDepthmapConstraints,
                        ICPConstraints<float>* icpConstraints,
                        const std::vector<FlowConstraints<float>*>& vectorOfFlowConstraints,
                        ImageConstraints<float>* imageConstraints,
                        Affine<float, 3, 3>& rigidMotion,
                        Eigen::Matrix3Xf faceVertices,
                        Eigen::VectorX<float>& pcaCoeffsNeck,
                        const std::vector<Eigen::VectorX<float>>& pcaCoeffsPrevFrames,
                        const Settings& settings,
                        std::vector<State>& states) const;

    void LoadFaceMeshLandmarks(const MeshLandmarks<float>& faceMeshLandmarks);
    void LoadEyeLeftMeshLandmarks(const MeshLandmarks<float>& eyeLeftMeshLandmarks);
    void LoadEyeRightMeshLandmarks(const MeshLandmarks<float>& eyeRightMeshLandmarks);
    void LoadTeethMeshLandmarks(const MeshLandmarks<float>& teethMeshLandmarks);

private:
    void UpdateSubsampled();

private:
    rt::PCARig m_pcaRig;

    //! mesh landmarks that are used for pca face tracking using the full rig
    MeshLandmarks<float> m_faceMeshLandmarks;
    MeshLandmarks<float> m_eyeLeftMeshLandmarks;
    MeshLandmarks<float> m_eyeRightMeshLandmarks;
    MeshLandmarks<float> m_teethMeshLandmarks;

    //! subsampled pca rig only containing vertices that are needed for tracking
    rt::PCARig m_pcaRigSubsampled;

    //! mesh landmarks that are used for pca face tracking using the subsampled rig
    MeshLandmarks<float> m_subsampledFaceMeshLandmarks;
    MeshLandmarks<float> m_subsampledEyeLeftMeshLandmarks;
    MeshLandmarks<float> m_subsampledEyeRightMeshLandmarks;
    MeshLandmarks<float> m_subsampledTeethMeshLandmarks;

    struct Private;
    std::unique_ptr<Private> m;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
