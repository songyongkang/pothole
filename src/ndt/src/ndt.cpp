#include "ndt.hpp"

NDT::NDT() 
{
	// subscriber init
	m_subVelodyne = m_nodeHandler.subscribe ("/points_ground", 100000, &NDT::VelodyneCallback, this);

	// publisher init
    m_pubAccumulateCloud = m_nodeHandler.advertise<sensor_msgs::PointCloud2>("/pothole/accumulate_cloud", 1000);

    SetParam();
	m_accumulateCloud.header.frame_id = m_strFrameName;
	m_inputTargetCloud.header.frame_id = m_strFrameName;
	m_inputSourceCloud.header.frame_id = m_strFrameName;

	m_bIsInitSource = true;

	Eigen::Translation3f tl_btol(0.0, 0.0, 0.0);                 // tl: translation
	Eigen::AngleAxisf rot_x_btol(0.0, Eigen::Vector3f::UnitX());  // rot: rotation
	Eigen::AngleAxisf rot_y_btol(0.0, Eigen::Vector3f::UnitY());
	Eigen::AngleAxisf rot_z_btol(0.0, Eigen::Vector3f::UnitZ());
	m_mat4fBase2Local = (tl_btol * rot_z_btol * rot_y_btol * rot_x_btol).matrix();
	m_mat4fLocal2Base = m_mat4fBase2Local.inverse();

	m_previousPose.x = 0.0;
	m_previousPose.y = 0.0;
	m_previousPose.z = 0.0;
	m_previousPose.roll = 0.0;
	m_previousPose.pitch = 0.0;
	m_previousPose.yaw = 0.0;

	m_ndtPose.x = 0.0;
	m_ndtPose.y = 0.0;
	m_ndtPose.z = 0.0;
	m_ndtPose.roll = 0.0;
	m_ndtPose.pitch = 0.0;
	m_ndtPose.yaw = 0.0;

	m_currentPose.x = 0.0;
	m_currentPose.y = 0.0;
	m_currentPose.z = 0.0;
	m_currentPose.roll = 0.0;
	m_currentPose.pitch = 0.0;
	m_currentPose.yaw = 0.0;

	m_diff_x = 0.0;
	m_diff_y = 0.0;
	m_diff_z = 0.0;
	m_diff_yaw = 0.0;
}
NDT::~NDT()
{

}
void NDT::Run()
{
	ros::Rate delay(10);
	
	while(ros::ok()) {
		ros::spinOnce();
		delay.sleep();
	}
}

void NDT::SetParam(void)
{
	m_nodeHandler.getParam("ndt/on_decription", m_bOnDiscription);
	m_nodeHandler.getParam("ndt/fixed_frame_name", m_strFrameName);
	m_nodeHandler.getParam("ndt/voxel_leafsize", m_dLeafSize_m);
	m_nodeHandler.getParam("ndt/x_max", m_dXMax);
	m_nodeHandler.getParam("ndt/x_min", m_dXMin);
	m_nodeHandler.getParam("ndt/y_max", m_dYMax);
	m_nodeHandler.getParam("ndt/y_min", m_dYMin);
	m_nodeHandler.getParam("ndt/z_max", m_dZMax);
	m_nodeHandler.getParam("ndt/z_min", m_dZMin);
}
void NDT::VelodyneCallback(const sensor_msgs::PointCloud2::ConstPtr& pInput)
{
    // Container for input data 
	PointCloudXYZ tmpCloud;
	pcl::fromROSMsg(*pInput, tmpCloud);
	pPointCloudXYZ pTmpCloud (new PointCloudXYZ(tmpCloud));


	if (m_bIsInitSource) {
		pPointCloudXYZ pTransformedCloud (new PointCloudXYZ);
		pcl::transformPointCloud (*pTmpCloud, *pTransformedCloud, m_mat4fBase2Local);
		SetInputTargetCloud(pTransformedCloud);
	}


	pPointCloudXYZ pDownsampledCloud (new PointCloudXYZ);
	
	m_ndt.setTransformationEpsilon(0.01);
	m_ndt.setStepSize(0.1);
	m_ndt.setResolution(0.4);
	m_ndt.setMaximumIterations(30);
	//m_ndt.setInputSource (pDownsampledCloud);
	m_ndt.setInputSource (pTmpCloud);

	if (m_bIsInitSource)
	{
		m_ndt.setInputTarget (GetInputTargetCloud());
		m_bIsInitSource = false;
	}

	pose guess_pose;

	guess_pose.x = m_previousPose.x + m_diff_x;	// what is the coordinate of guess_pose
	guess_pose.y = m_previousPose.y + m_diff_y;
	guess_pose.z = m_previousPose.z + m_diff_z;
	guess_pose.roll = m_previousPose.roll;
	guess_pose.pitch = m_previousPose.pitch;
	guess_pose.yaw = m_previousPose.yaw + m_diff_yaw;

	Eigen::AngleAxisf init_rotation_x (guess_pose.roll, Eigen::Vector3f::UnitX());
	Eigen::AngleAxisf init_rotation_y (guess_pose.pitch, Eigen::Vector3f::UnitY());
	Eigen::AngleAxisf init_rotation_z (guess_pose.yaw, Eigen::Vector3f::UnitZ());

	Eigen::Translation3f init_translation(guess_pose.x, guess_pose.y, guess_pose.z);
	Eigen::Matrix4f init_guess =
		(init_translation * init_rotation_z * init_rotation_y * init_rotation_x).matrix() * m_mat4fBase2Local;


	Eigen::Matrix4f mat4fLocalizer (Eigen::Matrix4f::Identity());
	Eigen::Matrix4f mat4fLocalizerInverse (Eigen::Matrix4f::Identity());
	pPointCloudXYZ pOutputCloud(new PointCloudXYZ);

	m_ndt.align(*pOutputCloud, init_guess);
	mat4fLocalizer = m_ndt.getFinalTransformation();
	mat4fLocalizerInverse = mat4fLocalizer.inverse();

	Eigen::Matrix4f mat4fBaseLink(Eigen::Matrix4f::Identity());
	Eigen::Matrix4f mat4fBaseLinkInverse(Eigen::Matrix4f::Identity());
	mat4fBaseLink = mat4fLocalizer * m_mat4fLocal2Base;
	mat4fBaseLinkInverse = mat4fBaseLink.inverse();

	tf::Matrix3x3 mat_b;

	mat_b.setValue(static_cast<double>(mat4fBaseLink(0, 0)), static_cast<double>(mat4fBaseLink(0, 1)),
			static_cast<double>(mat4fBaseLink(0, 2)), static_cast<double>(mat4fBaseLink(1, 0)),
			static_cast<double>(mat4fBaseLink(1, 1)), static_cast<double>(mat4fBaseLink(1, 2)),
			static_cast<double>(mat4fBaseLink(2, 0)), static_cast<double>(mat4fBaseLink(2, 1)),
			static_cast<double>(mat4fBaseLink(2, 2)));

	// Update m_ndtPose.
	m_ndtPose.x = mat4fBaseLink(0, 3);
	m_ndtPose.y = mat4fBaseLink(1, 3);
	m_ndtPose.z = mat4fBaseLink(2, 3);
	mat_b.getRPY(m_ndtPose.roll, m_ndtPose.pitch, m_ndtPose.yaw, 1);

	m_currentPose.x = m_ndtPose.x;
	m_currentPose.y = m_ndtPose.y;
	m_currentPose.z = m_ndtPose.z;
	m_currentPose.roll = m_ndtPose.roll;
	m_currentPose.pitch = m_ndtPose.pitch;
	m_currentPose.yaw = m_ndtPose.yaw;

	// Calculate the offset (curren_pos - previous_pos)
	m_diff_x = m_currentPose.x - m_previousPose.x;
	m_diff_y = m_currentPose.y - m_previousPose.y;
	m_diff_z = m_currentPose.z - m_previousPose.z;
	m_diff_yaw = calcDiffForRadian(m_currentPose.yaw, m_previousPose.yaw);

	// Update position and posture. current_pos -> previous_pos
	m_previousPose.x = m_currentPose.x;
	m_previousPose.y = m_currentPose.y;
	m_previousPose.z = m_currentPose.z;
	m_previousPose.roll = m_currentPose.roll;
	m_previousPose.pitch = m_currentPose.pitch;
	m_previousPose.yaw = m_currentPose.yaw;

	pPointCloudXYZ pTransformedInputTargetCloud (new PointCloudXYZ);
	pcl::transformPointCloud(*GetInputTargetCloud(), *pTransformedInputTargetCloud, mat4fBaseLinkInverse);


	*pTransformedInputTargetCloud += *pTmpCloud;

	pPointCloudXYZ pTmpPointCloud (new PointCloudXYZ);
	Voxelization (pTransformedInputTargetCloud, pTmpPointCloud);
	SetInputTargetCloud(pTmpPointCloud);

	m_ndt.setInputTarget (GetInputTargetCloud());


    // // Thresholding
	// pPointCloudXYZ pPassthroughCloud (new PointCloudXYZ);
	// if (!PassThrough(pTmpCloud, pPassthroughCloud)) ROS_ERROR_STREAM("Fail Pass Through");

	// // Voxelization
	// pPointCloudXYZ pDownsampledCloud (new PointCloudXYZ);
	// if (!Voxelization (pPassthroughCloud, pDownsampledCloud))ROS_ERROR_STREAM("Fail Voxel Grid Filter");
	// if (m_bOnDiscription){
	// 	std::cout << "Original: " << pPassthroughCloud->points.size() << " points." << std::endl;
	// 	std::cout << "Filtered: " << pDownsampledCloud->points.size() << " points." << std::endl;
	// }

	// Publish
	Publish();

	// SetPreprocessCloud(pDownsampledCloud);
}

void NDT::Publish(void)
{
    m_pubAccumulateCloud.publish(GetInputTargetCloud());
}

// bool NDT::PassThrough(cpPointCloudXYZ& pInputCloud,pPointCloudXYZ& pOutputCloud)
// {
//     // Thresholding 
// 	pcl::PassThrough<pcl::PointXYZ> passFilter;
// 	passFilter.setInputCloud (pInputCloud);
// 	// passFilter.setFilterFieldName("x");
// 	// passFilter.setFilterLimits (m_dXMin, m_dXMax); 	
// 	// passFilter.setFilterFieldName("z");
// 	// passFilter.setFilterLimits (m_dZMin, m_dZMax);
// 	passFilter.setFilterFieldName("y");
// 	passFilter.setFilterLimits (m_dXMin, m_dXMax); 	
// 	passFilter.filter (*pOutputCloud);	
	
// 	if (pOutputCloud->empty()) return false;
// 	else return true;
// }

bool NDT::Voxelization(cpPointCloudXYZ& pInputCloud,pPointCloudXYZ& pOutputCloud)
{
    // Voxel length of the corner : fLeafSize
	pcl::VoxelGrid<pcl::PointXYZ> voxelFilter;
	voxelFilter.setInputCloud (pInputCloud);
	voxelFilter.setLeafSize(m_dLeafSize_m, m_dLeafSize_m, m_dLeafSize_m);
	voxelFilter.filter (*pOutputCloud);

	if (pOutputCloud->empty()) return false;
	else return true;
}

double NDT::calcDiffForRadian(const double lhs_rad, const double rhs_rad)
{
	double diff_rad = lhs_rad - rhs_rad;
	if (diff_rad >= M_PI)
		diff_rad = diff_rad - 2 * M_PI;
	else if (diff_rad < -M_PI)
		diff_rad = diff_rad + 2 * M_PI;
	return diff_rad;
}