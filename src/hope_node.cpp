﻿// This cpp is for producing the experiment result in our papar titled:
//
// For a handy tool for extracting horizontal planes in ROS, please use hope_ros by

#include <geometry_msgs/TransformStamped.h>

#include <math.h>
#include <string.h>
#include <iostream> 
#include <iomanip>
#include <fstream>  

#include <cv_bridge/cv_bridge.h>

// PCL-ROS
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>
#include <pcl_conversions/pcl_conversions.h>

// PCL
#include <pcl/filters/extract_indices.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>

//OpenCV
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>

// Customized message
#include "lib/get_cloud.h"
#include "lib/plane_segment.h"
#include "lib/utilities.h"

//#define DEBUG

using namespace std;

// Publishers

/// Transform frame, only used with real time data
/// You may change the name based on your robot configuration
string base_frame_ = "base_link"; // world frame
string camera_optical_frame_ = "vision_depth_optical_frame";

/// Camera orientation params, only used for benchmarking
float roll_angle_ = 0.0;
float pitch_angle_ = 0.0;
float yaw_angle_ = 0.0;

float tx_, ty_, tz_, qx_, qy_, qz_, qw_;

void fraseInput(string input, vector<string> &vrgb, vector<string> &vdepth, 
                vector<vector<float> > &vec_xyzw)
{  
  ifstream list(input.c_str());
  
  if(!list){
    cout << "Unable to open the list" << endl;
    return;
  }
  char rgb[27];
  char depth[27];
  float s, tx, ty, tz, qx, qy, qz, qw;
  string line;

  while (getline(list, line)) {
    // http://www.cplusplus.com/reference/cstdio/scanf/
    sscanf(line.c_str(), "%f %s %s %f %f %f %f %f %f %f",
           &s, rgb, depth, &tx, &ty, &tz, &qx, &qy, &qz, &qw);
    ostringstream rgb_ost;
    rgb_ost << rgb;
    ostringstream dep_ost;
    dep_ost << depth;
    vrgb.push_back(rgb_ost.str());
    vdepth.push_back(dep_ost.str());
    tx_ = tx;
    ty_ = ty;
    tz_ = tz;
    vector<float> q;
    q.push_back(qx);
    q.push_back(qy);
    q.push_back(qz);
    q.push_back(qw);
    vec_xyzw.push_back(q);
  }
}

int main(int argc, char **argv)
{
  // You need to run roscore to launch this program
  ros::init(argc, argv, "hope_node");
  
  // TUM
  string path_prefix;
  string path_rgb;
  string path_depth;
  string path_list_all;
  data_type type;

  // Indoor RGB-D && synthesized
  string path_cloud;
  string cloud_type;

  int arg_index = 1;

  if (argc == 2) {
    // Test on a series of images, recommended
    // /home/omnisky/TUM/rgbd_dataset_freiburg1_desk
    int arg_index = 1;
    path_prefix = argv[arg_index++]; // path prefix of all.txt, see README for generating that
    path_list_all = path_prefix + "/all.txt";

    ROS_INFO("Using image list of TUM RGB-D SLAM dataset.");
    type = TUM_LIST;
  }
  else if (argc == 3) {
    // Test on a PCD or PLY point cloud
    // /home/omnisky/loft.ply ply
    int arg_index = 1;
    path_cloud = argv[arg_index++]; // path prefix of all.txt, see README for generating that
    cloud_type = argv[arg_index++];
    ROS_INFO("Using point cloud file.");
    type = POINT_CLOUD;
  }
  else if (argc == 4) {
    type = SYN;
    roll_angle_ = atof(argv[arg_index++]);
    pitch_angle_ = atof(argv[arg_index++]);
    yaw_angle_ = atof(argv[arg_index++]);
    ROS_INFO("Using syntheic data.");
  }
  else if (argc == 11) {
    // Test on a single image pair
    // /home/omnisky/TUM/rgbd_dataset_freiburg1_desk/ rgb/1305031459.259760.png depth/1305031459.274941.png -0.2171 -0.0799 1.3959 -0.8445 -0.0451 0.0954 0.5251
    // /home/omnisky/TUM/rgbd_dataset_freiburg1_desk/ rgb/1305031460.727675.png depth/1305031460.711684.png -0.6860 0.2286 1.4211 0.7862 -0.4513 0.1312 -0.4012
    // /home/omnisky/TUM/rgbd_dataset_freiburg1_desk/ rgb/1305031467.027843.png depth/1305031467.020424.png 1.4268 -0.2879 1.4371 0.8091 0.3318 -0.2030 -0.4405
    // /home/omnisky/TUM/rgbd_dataset_freiburg1_desk/ rgb/1305031473.196069.png depth/1305031473.190828.png 0.9453 -0.0038 1.2888 0.8978 0.2746 -0.0950 -0.3310
    int arg_index = 1;
    path_prefix = argv[arg_index++];
    path_rgb = path_prefix + argv[arg_index++];
    path_depth = path_prefix + argv[arg_index++];
    
    tx_ = atof(argv[arg_index++]);
    ty_ = atof(argv[arg_index++]);
    tz_ = atof(argv[arg_index++]);
    qx_ = atof(argv[arg_index++]);
    qy_ = atof(argv[arg_index++]);
    qz_ = atof(argv[arg_index++]);
    qw_ = atof(argv[arg_index++]);
    ROS_INFO("Using single pair of rgb and depth image of TUM RGB-D SLAM dataset.");
    type = TUM_SINGLE;
  }
  else {
    cerr << "Argument number should be 2, 3, 4, or 11" << endl;
    return -1;
  }
  
//  float xy_resolution = 0.02; // In meter
//  float z_resolution = 0.004; // In meter
  float xy_resolution = 0.05; // In meter
  float z_resolution = 0.02; // In meter
  cout << "Using threshold: xy@" << xy_resolution << " " << "z@" << z_resolution << endl;

  PlaneSegment hope(base_frame_, xy_resolution, z_resolution);
  PointCloud::Ptr src_cloud(new PointCloud); // Cloud input for all pipelines

  hope.setMode(type);
  if (type == REAL) {
    while (true) {
      // The src_cloud is actually not used here
      hope.getHorizontalPlanes(src_cloud);
    }
  }
  else if (type == SYN) {
    // -2.0944 0 0
    hope.setRPY(roll_angle_, pitch_angle_, yaw_angle_);
    while (true) {
      hope.getHorizontalPlanes(src_cloud);
    }
  }
  else {
    if (type == POINT_CLOUD) {
      if (cloud_type == "pcd") {
        if (pcl::io::loadPCDFile<pcl::PointXYZRGB> (path_cloud, *src_cloud) == -1) {
          cerr << "Couldn't read pcd file." << endl;
          return -1;
        }
      }
      else if (cloud_type == "ply") {
        pcl::PLYReader Reader;
        Reader.read(path_cloud, *src_cloud);
      }
      else {
        cerr << "Unrecognized file format." << endl;
        return -1;
      }
      hope.setQ(qx_, qy_, qz_, qw_);
      hope.getHorizontalPlanes(src_cloud);
    }

    cv::Mat rgb;
    cv::Mat depth;
    GetCloud m_gc;

    if (type == TUM_SINGLE) {
      // Set camera pose for point cloud transferation
      hope.setQ(qx_, qy_, qz_, qw_);
      hope.setT(tx_, ty_, tz_);
      
      // Precaptured images are used for testing on benchmarks
      rgb = cv::imread(path_rgb);
      depth = cv::imread(path_depth, -1); // Using flag<0 to read the image without changing its type

#ifdef DEBUG
    // The rgb images from TUM dataset are in CV_8UC3 type while the depth images are in CV_16UC1
    // The rgb image should be phrased with Vec3b while the depth with ushort
    cout << "Image type: rgb: " << rgb.type() << " depth: " << depth.type() << endl;
    cv::imshow("rgb", rgb);
    cv::imshow("depth", depth);
    cv::waitKey();
#endif

      // Filter the cloud with range 0.3-8.0m cause most RGB-D sensors are unreliable outside this range
      // But if Lidar data are used, try expanding the range
      // TODO add Nan filter in this function
      m_gc.getColorCloud(rgb, depth, src_cloud, 8.0, 0.3);
      hope.getHorizontalPlanes(src_cloud);
    }
    else if (type == TUM_LIST) {
      vector<string> fnames_rgb;
      vector<string> fnames_depth;
      vector<vector<float> > vec_xyzw;
      fraseInput(path_list_all, fnames_rgb, fnames_depth, vec_xyzw);
      for (size_t i = 0; i < fnames_rgb.size(); ++i) {
        
        hope.setQ(vec_xyzw[i][0], vec_xyzw[i][1], vec_xyzw[i][2], vec_xyzw[i][3]);
        hope.setT(tx_, ty_, tz_);
        
        cout << "Processing: " << fnames_rgb[i] << endl;
        rgb = cv::imread(path_prefix + "/" + fnames_rgb[i]);
        depth = cv::imread(path_prefix + "/" + fnames_depth[i], -1);

        // Filter the cloud with range 0.3-8.0m cause most RGB-D sensors are unreliable outside this range
        // But if Lidar data are used, try expanding the range
        m_gc.getColorCloud(rgb, depth, src_cloud, 8.0, 0.3);
        hope.getHorizontalPlanes(src_cloud);
      }
    }
  }

  return 0;
}
