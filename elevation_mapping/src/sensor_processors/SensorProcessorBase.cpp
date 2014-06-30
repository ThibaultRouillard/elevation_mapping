/*
 * SensorProcessorBase.cpp
 *
 *  Created on: Jun 6, 2014
 *      Author: Péter Fankhauser, Hannes Keller
 *   Institute: ETH Zurich, Autonomous Systems Lab
 */

#include <elevation_mapping/sensor_processors/SensorProcessorBase.hpp>

//PCL
#include <pcl/common/transforms.h>
#include <pcl/common/io.h>

//TF
#include <tf_conversions/tf_eigen.h>

namespace elevation_mapping {

SensorProcessorBase::SensorProcessorBase(tf::TransformListener& transformListener)
    : transformListener_(transformListener),
      mapFrameId_(""),
      baseFrameId_("")
{
	transformationSensorToMap_.setIdentity();
	transformListenerTimeout_.fromSec(1.0);
}

SensorProcessorBase::~SensorProcessorBase() {}

bool SensorProcessorBase::process(
		const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr pointCloudInput,
		const Eigen::Matrix<double, 6, 6>& robotPoseCovariance,
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloudOutput,
		Eigen::VectorXf& variances)
{
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloudClean(new pcl::PointCloud<pcl::PointXYZRGB>);
	pcl::copyPointCloud(*pointCloudInput, *pointCloudClean);
	cleanPointCloud(pointCloudClean);

	ros::Time timeStamp;
#if ROS_VERSION_MINIMUM(1, 10, 0) // Hydro and newer
	timeStamp.fromNSec(1000.0 * pointCloudClean->header.stamp); // TODO Double check.
#else
	timeStamp = pointCloudClean->header.stamp;
#endif

	if (!updateTransformations(pointCloudClean->header.frame_id, timeStamp)) return false;

	if (!transformPointCloud(pointCloudClean, pointCloudOutput, mapFrameId_)) return false;

	if (!computeVariances(pointCloudClean, robotPoseCovariance, variances)) return false;

	return true;
}

bool SensorProcessorBase::updateTransformations(const std::string& sensorFrameId, const ros::Time& timeStamp)
{
	try
	{
		transformListener_.waitForTransform(sensorFrameId, mapFrameId_, timeStamp, ros::Duration(1.0));

		tf::StampedTransform transformTf;
		transformListener_.lookupTransform(mapFrameId_, sensorFrameId, timeStamp, transformTf);
		poseTFToEigen(transformTf, transformationSensorToMap_);

		transformListener_.lookupTransform(baseFrameId_, sensorFrameId, timeStamp, transformTf); // TODO Why wrong direction?
		Eigen::Affine3d transform;
		poseTFToEigen(transformTf, transform);
		rotationBaseToSensor_.setMatrix(transform.rotation().matrix());
		translationBaseToSensorInBaseFrame_.toImplementation() = transform.translation();

		transformListener_.lookupTransform(mapFrameId_, baseFrameId_, timeStamp, transformTf); // TODO Why wrong direction?
		poseTFToEigen(transformTf, transform);
		rotationMapToBase_.setMatrix(transform.rotation().matrix());
		translationMapToBaseInMapFrame_.toImplementation() = transform.translation();

		return true;
	}
	catch (tf::TransformException &ex)
	{
		ROS_ERROR("%s", ex.what());
		return false;
	}
}

bool SensorProcessorBase::transformPointCloud(
		const pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloud,
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloudTransformed,
		const std::string& targetFrame)
{
	pcl::transformPointCloud(*pointCloud, *pointCloudTransformed, transformationSensorToMap_.cast<float>());
	pointCloudTransformed->header.frame_id = targetFrame;

	ROS_DEBUG("ElevationMap: Point cloud transformed to frame %s for time stamp %f.", targetFrame.c_str(),
			ros::Time(pointCloudTransformed->header.stamp).toSec());
	return true;
}

} /* namespace elevation_mapping */