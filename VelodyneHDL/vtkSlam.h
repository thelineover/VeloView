//=========================================================================
//
// Copyright 2018 Kitware, Inc.
// Author: Guilbert Pierre (spguilbert@gmail.com)
// Data: 03-27-2018
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//=========================================================================

// This slam algorithm is largely inspired by the LOAM algorithm:
// J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
// Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

// The algorithm is composed of three sequential steps:
//
// - Keypoints extraction: this step consists of extracting keypoints over
// the points clouds. To do that, the laser lines / scans are trated indepently.
// The laser lines are projected onto the XY plane and are rescale depending on
// their vertical angle. Then we compute their curvature and create two class of
// keypoints. The edges keypoints which correspond to points with a hight curvature
// and planar points which correspond to points with a low curvature.
//
// - Ego-Motion: this step consists of recovering the motion of the lidar
// sensor between two frames (two sweeps). The motion is modelized by a constant
// velocity and angular velocity between two frames (i.e null acceleration). 
// Hence, we can parameterize the motion by a rotation and translation per sweep / frame
// and interpolate the transformation inside a frame using the timestamp of the points.
// Since the points clouds generated by a lidar are sparses we can't design a
// pairwise match between keypoints of two successive frames. Hence, we decided to use
// a closest-point matching between the keypoints of the current frame
// and the geometrics features derived from the keypoints of the previous frame.
// The geometrics features are lines or planes and are computed using the edges keypoints
// and planar keypoints of the previous frame. Once the matching is done, a keypoint
// of the current frame is matched with a plane / line (depending of the
// nature of the keypoint) from the previous frame. Then, we recover R and T by
// minimizing the function f(R, T) = sum(d(point, line)^2) + sum(d(point, plane)^2).
// Which can be writen f(R, T) = sum((R*X+T-P).t*A*(R*X+T-P)) where:
// - X is a keypoint of the current frame
// - P is a point of the corresponding line / plane
// - A = (n*n.t) with n being the normal of the plane
// - A = (I - n*n.t).t * (I - n*n.t) with n being a director vector of the line
// Since the function f(R, T) is a non-linear mean square error function
// we decided to use the Levenberg-Marquardt algorithm to recover its argmin.
//
// - Mapping: This step consists of refining the motion recovered in the Ego-Motion
// step and to add the new frame in the environment map. Thanks to the ego-motion
// recovered at the previous step it is now possible to estimate the new position of
// the sensor in the map. We use this estimation as an initial point (R0, T0) and we
// perform an optimization again using the keypoints of the current frame and the matched
// keypoints of the map (and not only the previous frame this time!). Once the position in the
// map has been refined from the first estimation it is then possible to update the map by
// adding the keypoints of the current frame into the map.
//
// In the following programs : "vtkSlam.h" and "vtkSlam.cxx" the lidar
// coordinate system {L} is a 3D coordinate system with its origin at the
// geometric center of the lidar. The world coordinate system {W} is a 3D
// coordinate system which coinciding with {L] at the initial position. The
// points will be denoted by the ending letter L or W if they belong to
// the corresponding coordinate system

#ifndef VTK_SLAM_H
#define VTK_SLAM_H

#define slamGetMacro(prefix,name,type) \
type Get##prefix##_##name () const\
  { \
  return this->name; \
  }

#define slamSetMacro(prefix,name,type) \
void Set##prefix##_##name (const type _arg) \
{ \
  this->name = _arg; \
}

// LOCAL
#include "vtkPCLConversions.h"
// STD
#include <string>
#include <ctime>
// VTK
#include <vtkPolyDataAlgorithm.h>
#include <vtkSmartPointer.h>
// EIGEN
#include <Eigen/Dense>
// PCL
#include <pcl/point_types.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/kdtree/kdtree_flann.h>


class vtkVelodyneTransformInterpolator;
class RollingGrid;
typedef pcl::PointXYZINormal Point;

class VTK_EXPORT vtkSlam : public vtkPolyDataAlgorithm
{
public:
  // vtkPolyDataAlgorithm functions
  static vtkSlam *New();
  vtkTypeMacro(vtkSlam, vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent);
  int CanReadFile(const char* fname);

  // Add a new frame to process to the slam algorithm
  // From this frame; keypoints will be computed and extracted
  // in order to recover the ego-motion of the lidar sensor
  // and to update the map using keypoints and ego-motion
  void AddFrame(vtkPolyData* newFrame);

  // Reset the algorithm. Notice that this function
  // will erase the map and all transformations that
  // have been computed so far
  void ResetAlgorithm();

  // Provide the calibration of the current sensor.
  // The mapping indicates the number of laser and
  // the mapping of the laser id
  void SetSensorCalibration(int* mapping, int nbLaser);

  // Indicate if the sensor calibration: number
  // of lasers and mapping of the laser id has been
  // provided earlier
  bool GetIsSensorCalibrationProvided();

  // Get the computed world transform so far
  void GetWorldTransform(double* Tworld);

  // Only compute the keypoint extraction to display result
  // This function is usefull for debugging
  void OnlyComputeKeypoints(vtkSmartPointer<vtkPolyData> newFrame);

  // Get/Set General
  slamGetMacro(,DisplayMode, bool)
  slamSetMacro(,DisplayMode, bool)

  slamGetMacro(,MaxDistBetweenTwoFrames, double)
  slamSetMacro(,MaxDistBetweenTwoFrames, double)

  slamGetMacro(,AngleResolution, double)
  slamSetMacro(,AngleResolution, double)

  // Get/Set RollingGrid
  /*const*/ unsigned int Get_RollingGrid_VoxelSize() const;
  void Set_RollingGrid_VoxelSize(const unsigned int size);

  void Get_RollingGrid_Grid_NbVoxel(double nbVoxel[3]) const;
  void Set_RollingGrid_Grid_NbVoxel(const double nbVoxel[3]);

  void Get_RollingGrid_PointCloud_NbVoxel(double nbVoxel[3]) const;
  void Set_RollingGrid_PointCloud_NbVoxel(const double nbVoxel[3]);

  /*const*/ double Get_RollingGrid_LeafVoxelFilterSize() const;
  void Set_RollingGrid_LeafVoxelFilterSize(const double size);

  // Get/Set Keypoint
  slamGetMacro(_Keypoint,MaxEdgePerScanLine, unsigned int)
  slamSetMacro(_Keypoint,MaxEdgePerScanLine, unsigned int)

  slamGetMacro(_Keypoint,MaxPlanarsPerScanLine, unsigned int)
  slamSetMacro(_Keypoint,MaxPlanarsPerScanLine, unsigned int)

  slamGetMacro(_Keypoint,MinDistanceToSensor, double)
  slamSetMacro(_Keypoint,MinDistanceToSensor, double)

  slamGetMacro(_Keypoint,PlaneCurvatureThreshold, double)
  slamSetMacro(_Keypoint,PlaneCurvatureThreshold, double)

  slamGetMacro(_Keypoint,EdgeCurvatureThreshold, double)
  slamSetMacro(_Keypoint,EdgeCurvatureThreshold, double)

  // Get/Set EgoMotion
  slamGetMacro(,EgoMotionMaxIter, unsigned int)
  slamSetMacro(,EgoMotionMaxIter, unsigned int)

  slamGetMacro(,EgoMotionIcpFrequence, unsigned int)
  slamSetMacro(,EgoMotionIcpFrequence, unsigned int)

  slamGetMacro(,EgoMotionLineDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,EgoMotionLineDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,EgoMotionLineDistancefactor, double)
  slamSetMacro(,EgoMotionLineDistancefactor, double)

  slamGetMacro(,EgoMotionPlaneDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,EgoMotionPlaneDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,EgoMotionPlaneDistancefactor1, double)
  slamSetMacro(,EgoMotionPlaneDistancefactor1, double)

  slamGetMacro(,EgoMotionPlaneDistancefactor2, double)
  slamSetMacro(,EgoMotionPlaneDistancefactor2, double)

  slamGetMacro(,EgoMotionMaxLineDistance, double)
  slamSetMacro(,EgoMotionMaxLineDistance, double)

  slamGetMacro(,EgoMotionMaxPlaneDistance, double)
  slamSetMacro(,EgoMotionMaxPlaneDistance, double)

  // Get/Set Mapping
  slamGetMacro(,MappingMaxIter, unsigned int)
  slamSetMacro(,MappingMaxIter, unsigned int)

  slamGetMacro(,MappingIcpFrequence, unsigned int)
  slamSetMacro(,MappingIcpFrequence, unsigned int)

  slamGetMacro(,MappingLineDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,MappingLineDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,MappingLineDistancefactor, double)
  slamSetMacro(,MappingLineDistancefactor, double)

  slamGetMacro(,MappingPlaneDistanceNbrNeighbors, unsigned int)
  slamSetMacro(,MappingPlaneDistanceNbrNeighbors, unsigned int)

  slamGetMacro(,MappingPlaneDistancefactor1, double)
  slamSetMacro(,MappingPlaneDistancefactor1, double)

  slamGetMacro(,MappingPlaneDistancefactor2, double)
  slamSetMacro(,MappingPlaneDistancefactor2, double)

  slamGetMacro(,MappingMaxLineDistance, double)
  slamSetMacro(,MappingMaxLineDistance, double)

  slamGetMacro(,MappingMaxPlaneDistance, double)
  slamSetMacro(,MappingMaxPlaneDistance, double)

  // Get/Set EgoMotion and Mapping
  slamGetMacro(,MinPointToLineOrEdgeDistance, double)
  slamSetMacro(,MinPointToLineOrEdgeDistance, double)



protected:
  // vtkPolyDataAlgorithm functions
  vtkSlam();
  ~vtkSlam();
  virtual int RequestData(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
  virtual int RequestDataObject(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
  virtual int RequestInformation(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
  virtual int RequestUpdateExtent(vtkInformation *, vtkInformationVector **, vtkInformationVector * );
private:
  vtkSlam(const vtkSlam&);
  void operator = (const vtkSlam&);
  // Polydata which represents the trajectory computed
  vtkSmartPointer<vtkPolyData> Trajectory;

  // Current point cloud stored in two differents
  // formats: PCL-pointcloud and vtkPolyData
  vtkSmartPointer<vtkPolyData> vtkCurrentFrame;
  vtkSmartPointer<vtkPolyData> vtkProcessedFrame;
  pcl::PointCloud<Point>::Ptr pclCurrentFrame;
  std::vector<pcl::PointCloud<Point>::Ptr> pclCurrentFrameByScan;
  std::vector<std::pair<int, int> > FromVTKtoPCLMapping;
  std::vector<std::vector<int > > FromPCLtoVTKMapping;

  // keypoints extracted
  pcl::PointCloud<Point>::Ptr CurrentEdgesPoints;
  pcl::PointCloud<Point>::Ptr CurrentPlanarsPoints;
  pcl::PointCloud<Point>::Ptr PreviousEdgesPoints;
  pcl::PointCloud<Point>::Ptr PreviousPlanarsPoints;

  // keypoints local map
  RollingGrid* EdgesPointsLocalMap;
  RollingGrid* PlanarPointsLocalMap;

  // Mapping of the lasers id
  std::vector<int> LaserIdMapping;

  // Curvature and over differntial operations
  // scan by scan; point by point
  std::vector<std::vector<std::pair<double, int> > > Curvature;
  std::vector<std::vector<double> > Gradient;
  std::vector<std::vector<std::pair<double, int> > > SecondDiff;
  std::vector<std::vector<std::pair<double, int> > > Angles;
  std::vector<std::vector<std::pair<double, int> > > DepthGap;
  std::vector<std::vector<int> > IsPointValid;
  std::vector<std::vector<int> > Label;

  // with of the neighbor used to compute discrete
  // differential operators
  int NeighborWidth;

  // Number of lasers scan lines composing the pointcloud
  unsigned int NLasers;

  // maximal angle resolution of the lidar
  double AngleResolution;

  // Number of frame that have been processed
  unsigned int NbrFrameProcessed;

  // minimal point/sensor sensor to consider a point as valid
  double MinDistanceToSensor;

  // Indicated the number max of keypoints
  // that we admit per laser scan line
  unsigned int MaxEdgePerScanLine;
  unsigned int MaxPlanarsPerScanLine;

  // Curvature threshold to select a point
  double EdgeCurvatureThreshold;
  double PlaneCurvatureThreshold;

  // The max distance allowed between two frames
  // If the distance is over this limit, the ICP
  // matching will not match point and the odometry
  // will fail. It has to be setted according to the
  // maximum speed of the vehicule used
  double MaxDistBetweenTwoFrames;

  // Maximum number of iteration
  // in the ego motion optimization step
  unsigned int EgoMotionMaxIter;
  unsigned int EgoMotionIterMade;

  // Maximum number of iteration
  // in the mapping optimization step
  unsigned int MappingMaxIter;
  unsigned int MappingIterMade;

  // During the Levenberg-Marquardt algoritm
  // keypoints will have to be match with planes
  // and lines of the previous frame. This parameter
  // indicates how many iteration we want to do before
  // running the closest-point matching again
  unsigned int EgoMotionIcpFrequence;
  unsigned int MappingIcpFrequence;

  // When computing the point<->line and point<->plane distance
  // in the ICP, the kNearest edges/planes points of the current
  // points are selected to approximate the line/plane using a PCA
  // If the one of the k-nearest points is too far the neigborhood
  // is rejected. We also make a filter upon the ratio of the eigen
  // values of the variance-covariance matrix of the neighborhood
  // to check if the points are distributed upon a line or a plane
  unsigned int MappingLineDistanceNbrNeighbors;
  double MappingLineDistancefactor;

  unsigned int MappingPlaneDistanceNbrNeighbors;
  double MappingPlaneDistancefactor1;
  double MappingPlaneDistancefactor2;

  double MappingMaxPlaneDistance;
  double MappingMaxLineDistance;

  unsigned int EgoMotionLineDistanceNbrNeighbors;
  double EgoMotionLineDistancefactor;

  unsigned int EgoMotionPlaneDistanceNbrNeighbors;
  double EgoMotionPlaneDistancefactor1;
  double EgoMotionPlaneDistancefactor2;

  double EgoMotionMaxPlaneDistance;
  double EgoMotionMaxLineDistance;

  double MinPointToLineOrEdgeDistance;
  // Transformation to map the current pointcloud
  // in the referential of the previous one
  Eigen::Matrix<double, 6, 1> Trelative;

  // Transformation to map the current pointcloud
  // in the world (i.e first frame) one
  Eigen::Matrix<double, 6, 1> Tworld;

  // Convert the input vtk-format pointcloud
  // into a pcl-pointcloud format. scan lines
  // will also be sorted by their vertical angles
  void ConvertAndSortScanLines(vtkSmartPointer<vtkPolyData> input);

  // Extract keypoints from the pointcloud. The key points
  // will be separated in two classes : Edges keypoints which
  // correspond to area with high curvature scan lines and
  // planar keypoints which have small curvature
  void ComputeKeyPoints(vtkSmartPointer<vtkPolyData> input);

  // Compute the curvature of the scan lines
  // The curvature is not the one of the surface
  // that intersected the lines but the curvature
  // of the scan lines taken in an isolated way
  void ComputeCurvature(vtkSmartPointer<vtkPolyData> input);

  // Invalid the points with bad criteria from
  // the list of possible future keypoints.
  // This points correspond to planar surface
  // roughtly parallel to laser beam and points
  // close to a gap created by occlusion
  void InvalidPointWithBadCriteria();

  // Labelizes point to be a keypoints or not
  void SetKeyPointsLabels(vtkSmartPointer<vtkPolyData> input);

  // Reset all mumbers variables that are
  // used during the process of a frame.
  // The map and the recovered transformations
  // won't be reset.
  void PrepareDataForNextFrame();

  // Find the ego motion of the sensor between
  // the current frame and the next one using
  // the keypoints extracted.
  void ComputeEgoMotion();

  // Map the position of the sensor from
  // the current frame in the world referential
  // using the map and the keypoints extracted.
  void Mapping();

  // Transform the input point acquired at time t1 to the
  // initial time t0. So that the deformation induced by
  // the motion of the sensor will be removed. We use the assumption
  // of constant angular velocity and velocity.
  void TransformToStart(Point& pi, Point& pf, Eigen::Matrix<double, 6, 1>& T);
  void TransformToStart(Eigen::Matrix<double, 3, 1>& Xi, Eigen::Matrix<double, 3, 1>& Xf, double s, Eigen::Matrix<double, 6, 1>& T);

  // Transform the input point acquired at time t1 to the
  // final time tf. So that the deformation induced by
  // the motion of the sensor will be removed. We use the assumption
  // of constant angular velocity and velocity.
  void TransformToEnd(Point& pi, Point& pf, Eigen::Matrix<double, 6, 1>& T);

  // All points of the current frame has been
  // acquired at a different timestamp. The goal
  // is to express them in a same referential
  // corresponding to the referential the end of the sweep.
  // This can be done using estimated egomotion and assuming
  // a constant angular velocity and velocity during a sweep
  void TransformCurrentKeypointsToEnd();

  // Transform the input point already undistort into Tworld.
  void TransformToWorld(Point& p, Eigen::Matrix<double, 6, 1>& T);

  // From the input point p, find the nearest edge line from
  // the previous point cloud keypoints
  void FindEdgeLineMatch(Point p, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges,
                         std::vector<int>& matchEdgeIndex1, std::vector<int>& matchEdgeIndex2, int currentEdgeIndex,
                         Eigen::Matrix<double, 3, 3> R, Eigen::Matrix<double, 3, 1> dT);

  // From the input point p, find the nearest plane from the
  // previous point cloud keypoints that match the input point
  void FindPlaneMatch(Point p, pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousPlanes,
                      std::vector<int>& matchPlaneIndex1, std::vector<int>& matchPlaneIndex2,
                      std::vector<int>& matchPlaneIndex3, int currentPlaneIndex,
                      Eigen::Matrix<double, 3, 3> R, Eigen::Matrix<double, 3, 1> dT);

  // From the line / plane match of the current keypoint, compute
  // the parameters of the distance function. The distance function is
  // (R*X+T - P).t * A * (R*X+T - P). These functions will compute the
  // parameters P and A.
  void ComputeLineDistanceParameters(std::vector<int>& matchEdgeIndex1, std::vector<int>& matchEdgeIndex2, unsigned int edgeIndex);
  void ComputePlaneDistanceParameters(std::vector<int>& matchPlaneIndex1, std::vector<int>& matchPlaneIndex2, std::vector<int>& matchPlaneIndex3, unsigned int planarIndex);

  // More accurate but slower
  void ComputeLineDistanceParametersAccurate(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousEdges, Eigen::Matrix<double, 3, 3>& R,
                                             Eigen::Matrix<double, 3, 1>& dT, Point p, std::string step);
  void ComputePlaneDistanceParametersAccurate(pcl::KdTreeFLANN<Point>::Ptr kdtreePreviousPlanes, Eigen::Matrix<double, 3, 3>& R,
                                              Eigen::Matrix<double, 3, 1>& dT, Point p, std::string step);

  // we want to minimize F(R,T) = sum(fi(R,T)^2)
  // for a given i; fi is called a residual value and
  // the jacobian of fi is called the residual jacobian
  void ComputeResidualValues(std::vector<Eigen::Matrix<double, 3, 3> >& vA, std::vector<Eigen::Matrix<double, 3, 1> >& vX,
                             std::vector<Eigen::Matrix<double, 3, 1> >& vP, Eigen::Matrix<double, 3, 3>& R,
                             Eigen::Matrix<double, 3, 1>& dT, Eigen::MatrixXd& residuals);
  void ComputeResidualJacobians(std::vector<Eigen::Matrix<double, 3, 3> >& vA, std::vector<Eigen::Matrix<double, 3, 1> >& vX,
                                std::vector<Eigen::Matrix<double, 3, 1> >& vP, Eigen::Matrix<double, 6, 1>& T,
                                Eigen::MatrixXd& residualsJacobians);


  // Update the world transformation by integrating
  // the relative motion recover and the previous
  // world transformation
  void UpdateTworldUsingTrelative();

  // To recover the ego-motion we have to minimize the function
  // f(R, T) = sum(d(point, line)^2) + sum(d(point, plane)^2). In both
  // case the distance between the point and the line / plane can be
  // writen (R*X+T - P).t * A * (R*X+T - P). Where X is the key point
  // P is a point on the line / plane. A = (n*n.t) for a plane with n
  // being the normal and A = (I - n*n.t)^2 for a line with n being
  // a director vector of the line
  // - Avalues will store the A matrix
  // - Pvalues will store the P points
  // - Xvalues will store the W points
  // - TimeValues store the time acquisition
  std::vector<Eigen::Matrix<double, 3, 3> > Avalues;
  std::vector<Eigen::Matrix<double, 3, 1> > Pvalues;
  std::vector<Eigen::Matrix<double, 3, 1> > Xvalues;
  std::vector<double> TimeValues;
  void ResetDistanceParameters();


  // Display infos
  void DisplayLaserIdMapping(vtkSmartPointer<vtkPolyData> input);
  void DisplayRelAdv(vtkSmartPointer<vtkPolyData> input);
  void DisplayKeypointsResults(vtkSmartPointer<vtkPolyData> input);
  void DisplayCurvatureScores(vtkSmartPointer<vtkPolyData> input);
  void DisplayRollingGrid(vtkSmartPointer<vtkPolyData> input);

  // Indicate if we are in display mode or not
  // Display mode will add arrays showing some
  // results of the slam algorithm such as
  // the keypoints extracted, curvature etc
  bool DisplayMode;
  
  // Time processing information
  void InitTime();
  void StopTimeAndDisplay(std::string functionName);
  std::clock_t Timer1, Timer2;

  // Identity matrix
  Eigen::Matrix<double, 3, 3> I3;
  Eigen::Matrix<double, 6, 6> I6;
};

#endif // VTK_SLAM_H
