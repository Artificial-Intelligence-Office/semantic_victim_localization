#include <ssd_keras/victim_vision_detector.h>

victim_vision_detector::victim_vision_detector():
victim_detector_base(),
ServiceCallFirstTime(true)
{

detection_Cluster_succeed=false;

tf_listener = new tf::TransformListener();


//Parameters
std::string topic_ssd_keras;
std::string topic_depth_image;
std::string topic_Pose;
std::string topic_Position;
std::string topic_segmented_PointCloud;

ros::param::param("~topic_ssd_keras", topic_ssd_keras, std::string("/ssd_detction/box"));
ros::param::param("~topic_depth_image", topic_depth_image, std::string("/floating_sensor/camera/depth/image_raw"));
ros::param::param("~topic_Pose", topic_Pose, std::string("/floating_sensor/poseStamped"));
ros::param::param("~topic_setPose", topic_Position, std::string("floating_sensor/set_pose"));
ros::param::param("~topic_segmented_PointCloud", topic_segmented_PointCloud, std::string("ssd/segmented_PointCloud"));
ros::param::param("~RGB_image_x_resolution", image_x_resol, 640.0);
ros::param::param("~RGB_image_y_resolution", image_y_resol, 480.0);
ros::param::param("~RGB_image_x_offset", image_x_offset, 320.5);
ros::param::param("~RGB_image_y_offset", image_y_offset, 240.5);
ros::param::param("~RGB_focal_length", RGB_FL,554.254691191187);//524.2422531097977);

ros::param::param<std::string>("~camera_optical_frame", camera_optical_frame , "/floating_sensor/camera_depth_optical_frame");


//message_filters configurations
depth_in_  = new message_filters::Subscriber<sensor_msgs::Image>(nh_, topic_depth_image, 1);
loc_sub_  = new message_filters::Subscriber<geometry_msgs::PoseStamped>(nh_, topic_Pose, 1);
sync = new message_filters::Synchronizer<MySyncPolicy>(MySyncPolicy(4), *depth_in_ ,*loc_sub_);

// Callbacks
sync->registerCallback(boost::bind(&victim_vision_detector::CallBackData, this, _1, _2));

//publisher topics
pub_segemented_human_pointcloud = nh_.advertise<sensor_msgs::PointCloud2>(topic_segmented_PointCloud, 4);

//client to interact with Python SSD_detection Code
pub_setpoint = nh_.advertise<geometry_msgs::PoseStamped>(topic_Position, 10);
client = nh_private.serviceClient<victim_localization::DL_box>("SSD_Detection");
}

victim_vision_detector::~victim_vision_detector(){}


void victim_vision_detector::CallBackData(const sensor_msgs::ImageConstPtr& input_depth,
                                                 const geometry_msgs::PoseStamped::ConstPtr& loc)
{

  try
  {
    current_depth_image = cv_bridge::toCvCopy(input_depth, sensor_msgs::image_encodings::TYPE_32FC1);
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }

  current_ps= *loc;
  }

void victim_vision_detector::DetectionService(){

  Detection_success=false;
  ros::Rate loop_rate(10);

  if(ServiceCallFirstTime) Start_time=ros::Time::now();

  while (ros::ok() && !Detection_success)
  {

    std::stringstream ss;
    ss << "Start Detecting";
    srv.request.req = ss.str();

    if (client.call(srv)) {
      Detection_success=true;
    }
    std::cout << cc.red << "SSD detecotr is not running, Retrying to connect\n" << cc.reset;

    ros::spinOnce();
    loop_rate.sleep();
  }

  victim_localization::DL_msgs_box box_;
  box_.xmax=srv.response.xmax;  box_.xmin=srv.response.xmin;
  box_.ymax=srv.response.ymax;  box_.ymin=srv.response.ymin;
  box_.Class=srv.response.Class;

  current_ssd_detection= box_;
  capture_ps= current_ps;
  if(ServiceCallFirstTime)
  {
    Service_startTime=ros::Time::now()-Start_time;
    ServiceCallFirstTime=false;
  }
}

ros::Duration victim_vision_detector::GetConnectionTime()
{
  return Service_startTime;
}

void victim_vision_detector::FindClusterCentroid(){  //TOFIX:: this code works with one human detection in the image, need to be
                                                            // generatlized for multiple humans

  pcl::PointCloud<pcl::PointXYZ>::Ptr depth_cloud (new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr filtered_cloud (new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_f (new pcl::PointCloud<pcl::PointXYZ>);

  detection_Cluster_succeed= false; //initialized detection status to start


  if (!DetectionAvailable()) {
    std::cout << "[Detection_Status:->] " << cc.yellow << "No Human is detected yet\n" << cc.reset;
    return;
   }
  else  std::cout << "[Detection_Status:->] " << cc.red << "A Human is detected \n" << cc.reset;

      int y_max=current_ssd_detection.ymax;
      int y_min=current_ssd_detection.ymin;
      int x_max=current_ssd_detection.xmax;
      int x_min=current_ssd_detection.xmin;

    // Fill in the cloud data
      depth_cloud->width    = (y_max-y_min) * (x_max-x_min);
      depth_cloud->height   = 1;
      depth_cloud->is_dense = false;
      depth_cloud->points.resize (depth_cloud->width * depth_cloud->height);


    int cnt=0;
    int cnt_non=0;
    for (int i=x_min; i<x_max; i++){
        for (int j=y_min; j<y_max; j++){
            int index=(j-y_min)+cnt*(y_max-y_min);
            float depth_=current_depth_image->image.at<float>(j,i);
            if (std::isnan(depth_)) cnt_non++;

            depth_cloud->points[index].x=(i- image_x_offset)*((depth_)/(RGB_FL));
            depth_cloud->points[index].y=(j- image_y_offset)*((depth_)/(RGB_FL));
            depth_cloud->points[index].z=depth_;
        }
        cnt++;
    }

      //remove non values from the point cloud
         std::vector<int> indices;
         pcl::removeNaNFromPointCloud(*depth_cloud, *depth_cloud, indices);

   //  Create the filtering object: downsample the dataset using a leaf size of 1cm
    //  pcl::VoxelGrid<pcl::PointXYZ> vg;
    //  vg.setInputCloud (depth_cloud);
    //  vg.setLeafSize (0.01f, 0.01f, 0.01f);
   //   vg.filter (*filtered_cloud);

      filtered_cloud = depth_cloud;
      // Create the segmentation object for the planar model and set all the parameters
      pcl::SACSegmentation<pcl::PointXYZ> seg;
      pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
      pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_plane (new pcl::PointCloud<pcl::PointXYZ> ());
      seg.setOptimizeCoefficients (true);
      seg.setModelType (pcl::SACMODEL_PLANE);
      seg.setMethodType (pcl::SAC_RANSAC);
      //seg.setMaxIterations (100);
      seg.setDistanceThreshold (0.01);

      int i=0, nr_points = (int) filtered_cloud->points.size ();
      while (filtered_cloud->points.size () > 0.3 * nr_points)
      {
        // Segment the largest planar component from the remaining cloud
        seg.setInputCloud (filtered_cloud);
        seg.segment (*inliers, *coefficients);
        if (inliers->indices.size () == 0)
        {
          break;
        }

        // Extract the planar inliers from the input cloud
        pcl::ExtractIndices<pcl::PointXYZ> extract;
        extract.setInputCloud (filtered_cloud);
        extract.setIndices (inliers);
        extract.setNegative (false);

        // Get the points associated with the planar surface
        extract.filter (*cloud_plane);

        // Remove the planar inliers, extract the rest
        extract.setNegative (true);
        extract.filter (*cloud_f);
        *filtered_cloud = *cloud_f;
      }

      std::cout << "filter_size is...." << filtered_cloud->points.size() << std::endl;
      if (!filtered_cloud->points.size()) {  // if filtered_cloud size is zero, we should try again with new input cloud data
        //detection_status="repeat";
        return;
          }

      // Creating the KdTree object for the search method of the extraction
      pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ>);
      tree->setInputCloud (filtered_cloud);

      std::vector<pcl::PointIndices> cluster_indices;
      pcl::EuclideanClusterExtraction<pcl::PointXYZ> ec;
      ec.setClusterTolerance (0.1); // 10cm
      ec.setMinClusterSize (10);
      ec.setMaxClusterSize (25000);
      ec.setSearchMethod (tree);
      ec.setInputCloud (filtered_cloud);
      ec.extract (cluster_indices);


    std::vector <pcl::PointCloud<pcl::PointXYZ>::Ptr, Eigen::aligned_allocator <pcl::PointCloud <pcl::PointXYZ>::Ptr > > cloud_clusters;
      int j = 0;
      for (std::vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin (); it != cluster_indices.end (); ++it)
      {
        pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_cluster (new pcl::PointCloud<pcl::PointXYZ>);
        for (std::vector<int>::const_iterator pit = it->indices.begin (); pit != it->indices.end (); ++pit)
        cloud_cluster->points.push_back((filtered_cloud->points[*pit])); //*
        cloud_cluster->width = cloud_cluster->points.size ();
        cloud_cluster->height = 1;
        cloud_cluster->is_dense = true;

        cloud_clusters.push_back(cloud_cluster);
        j++;
      }


     // getting the centroid of the cluster in world frame
        pcl::PointCloud<pcl::PointXYZ> temp_cloud;

        std::cout << "size of pointcloud segmented is.............." << cloud_clusters.size() << std::endl;
        if (cloud_clusters.size()==0){
         std::cout << "size of the cloud is small ignore...."  << std::endl;

          return;
}
        temp_cloud=*cloud_clusters[0];
        pcl::CentroidPoint<pcl::PointXYZ> centroid;

        while (temp_cloud.points.size()) {
            centroid.add(temp_cloud.points.back());
            temp_cloud.points.pop_back();
        }

        pcl::PointXYZ point_centroid;
        geometry_msgs::PointStamped point_centroid_gm;
        geometry_msgs::PointStamped point_out;

        centroid.get(point_centroid);
        point_centroid_gm.point=pose_conversion::convertToGeometryMsgPoint(point_centroid);
        point_centroid_gm.header.frame_id=camera_optical_frame;

        try
        {
          tf_listener->transformPoint("/world", point_centroid_gm, point_out);
        }
        catch (tf::TransformException &ex)
        {
          printf ("Failure %s\n", ex.what()); //Print exception which was caught
    return ;
        }

        detected_point = point_out.point;
        detection_Cluster_succeed= true;

        //std::cout << detected_point << std::endl;
        //PublishSegmentedPointCloud(*cloud_clusters[0]);
  }

void victim_vision_detector::PublishSegmentedPointCloud(const pcl::PointCloud<pcl::PointXYZ> input_PointCloud)
{
   if (input_PointCloud.size()==0) return;
   sensor_msgs::PointCloud2 output_msg;
   pcl::toROSMsg(input_PointCloud, output_msg); 	//cloud of original (white) using original cloud
   output_msg.header.frame_id = camera_optical_frame;
   output_msg.header.stamp = ros::Time::now();
   pub_segemented_human_pointcloud.publish(output_msg);
}


bool victim_vision_detector::DetectionAvailable(){
    return (current_ssd_detection.Class == "person");
}

Status victim_vision_detector::getDetectorStatus()
{
  Status status;
  status.victim_found = detection_Cluster_succeed;
  status.victim_loc[0]=detected_point.x;
  status.victim_loc[1]=detected_point.y;
  std::cout << "location is\n";
  std::cout << status.victim_loc[0]<< " " << status.victim_loc[1];
  return status;
}


void victim_vision_detector::performDetection(){
  DetectionService();
  FindClusterCentroid();
}

/*

int main(int argc, char **argv)
{
// >>>>>>>>>>>>>>>>>
// Initialize ROS
// >>>>>>>>>>>>>>>>>
ros::init(argc, argv, "clustering");
ros::NodeHandle ros_node;
ros::Rate rate(30);

SSD_Detection_with_clustering *cluster;

cluster= new SSD_Detection_with_clustering();

while (ros::ok) {
  cluster->FindClusterCentroid();
  std::cout << "status: " << cluster->detection_status << "\n";
     ros::spinOnce();
      rate.sleep() ;
}

return 0;

}

*/

