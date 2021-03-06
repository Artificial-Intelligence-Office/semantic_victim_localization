﻿#include <victim_localization/volumetric_map_manager.h>
#include <string.h>

Volumetric_Map::Volumetric_Map(volumetric_mapping::OctomapManager *manager):
  m_octree(NULL),
  m_gridmap(&gridMap),
  m_worldFrameId("/world"), m_baseFrameId("/base"),
  m_minSizeX(0.0), m_minSizeY(0.0) , accept_pointCloud(true), stop(false), count_pointCloud(0)
{
  ros::param::param<double>("~minmum_z_for_2D_map", m_occupancyMinZ , 1);
  ros::param::param<double>("~maximum_z_for_2D_map", m_occupancyMaxZ , 1);
  ros::param::param<bool>("~publish_2D_map", m_publish2DMap , true);
  ros::param::param<int>("~Number_of_recieved_pointCloud", num_recieved_pointCloud , 5);

  ros::param::param<std::string>("~topic_pointcloud",topic_pointcloud_,
                                 std::string("/floating_sensor/camera/depth/points"));

  ros::param::param<std::string>("~topic_Pose",topic_Pose,
                                 std::string("/floating_sensor/poseStamped"));

  //lastpose.position.x=0;lastpose.position.y=0;lastpose.position.z=0;
  manager_= manager;
//  pointcloud_in_  = new message_filters::Subscriber<sensor_msgs::PointCloud2>(nh_, topic_pointcloud_, 1);
//  loc_sub_  = new message_filters::Subscriber<geometry_msgs::PoseStamped>(nh_, topic_Pose, 1);
//  sync = new message_filters::Synchronizer<MySyncPolicy>(MySyncPolicy(30), *pointcloud_in_ ,*loc_sub_);

  // Callbacks
//  sync->registerCallback(boost::bind(&Volumetric_Map::callbackSetPointCloud, this, _1, _2));

  pointcloud_sub_ = nh_.subscribe(topic_pointcloud_, 1,  &Volumetric_Map::callbackSetPointCloud,this);

  m_octree = manager_->octree_;
  m_treeDepth = m_octree->getTreeDepth();
  m_maxTreeDepth = m_treeDepth;
  m_gridmap->info.resolution = m_octree->getResolution();

  m_mapPub = nh_.advertise<nav_msgs::OccupancyGrid>("projected_map", 1);
  previous_time= ros::Time::now();
}


//void Volumetric_Map::callbackSetPointCloud(const sensor_msgs::PointCloud2::ConstPtr &input_msg,
//                                           const geometry_msgs::PoseStamped::ConstPtr& loc)
void Volumetric_Map::callbackSetPointCloud(const sensor_msgs::PointCloud2::ConstPtr &input_msg)

{
  if (manager_ == NULL){
    ROS_ERROR_THROTTLE(1, "Map not set up: No octomap available!");
}

  if (accept_pointCloud){
    ros::Time current=ros::Time::now();
    manager_->insertPointcloudWithTf(input_msg);

    current=ros::Time::now();
    Convert2DMaptoOccupancyGrid(ros::Time::now());

    count_pointCloud+=1;
    if (stop) { accept_pointCloud=false;stop=false;}
  }
}
void Volumetric_Map::Convert2DMaptoOccupancyGrid(const ros::Time &rostime)
{
  if (m_octree->size() <= 1){
    ROS_WARN("Nothing to publish, octree is empty");
    return;
  }

  PrecheckFor2DMap(rostime);

  //  traverse all leafs in the tree:
  for (OcTreeT::iterator it = m_octree->begin(m_maxTreeDepth),
       end = m_octree->end(); it != end; ++it)
  {
    double z = it.getZ();

    octomap::point3d p = m_octree->keyToCoord(it.getKey());

    octomap::OcTreeNode* node = m_octree->search(it.getKey());



    //ignore the node that are outside the z-range
    if (z > m_occupancyMinZ && z < m_occupancyMaxZ){

      bool inUpdateBBX = isInUpdateBBX(it);

      if (IsnodeOccupied(it)==1){
        handleOccupiedNode(it);
        if (inUpdateBBX)
          handleOccupiedNodeInBBX(it);
      }


      else if (IsnodeOccupied(it)==2){ // node not occupied => mark as free in 2D map if unknown so far
        handleFreeNode(it);
        if (inUpdateBBX)
          handleFreeNodeInBBX(it);

      }
    }

  }
  Publish2DOccupancyMap();
}


int Volumetric_Map::IsnodeOccupied (const OcTreeT::iterator& it)  // 1 is occupied , 2 is free, 3 is unkonwn
{

  octomap::OcTreeNode* node = m_octree->search(it.getKey());

  if (node == NULL) {
    return 3;
  } else if (m_octree->isNodeOccupied(node)) {
    return 1;
  } else {
    return 2;
  }

  //double prob= node->getOccupancy();

  //if (prob>0.8)
  //  return 1;

  //if (prob<0.3)
  //  return 2;

  //return 3;
}





void Volumetric_Map::PrecheckFor2DMap(const ros::Time &rostime)  /* partially taken from
  https://github.com/OctoMap/octomap_mapping/blob/kinetic-devel/octomap_server/src/OctomapServer.cpp
  */
{
  // init projected 2D map:
  m_gridmap->header.frame_id = m_worldFrameId;
  m_gridmap->header.stamp = rostime;
  nav_msgs::MapMetaData oldMapInfo = m_gridmap->info;

  double minX, minY, minZ, maxX, maxY, maxZ;
  m_octree->getMetricMin(minX, minY, minZ);
  m_octree->getMetricMax(maxX, maxY, maxZ);

  octomap::point3d minPt(minX, minY, minZ);
  octomap::point3d maxPt(maxX, maxY, maxZ);
  octomap::OcTreeKey minKey = m_octree->coordToKey(minPt, m_maxTreeDepth);
  octomap::OcTreeKey maxKey = m_octree->coordToKey(maxPt, m_maxTreeDepth);

  ROS_DEBUG("MinKey: %d %d %d / MaxKey: %d %d %d", minKey[0], minKey[1], minKey[2], maxKey[0], maxKey[1], maxKey[2]);

  // add padding if requested (= new min/maxPts in x&y):
  //  double halfPaddedX = 0.5*m_minSizeX;
  //  double halfPaddedY = 0.5*m_minSizeY;
  //  minX = std::min(minX, -halfPaddedX);
  //  maxX = std::max(maxX, halfPaddedX);
  //  minY = std::min(minY, -halfPaddedY);
  //  maxY = std::max(maxY, halfPaddedY);
  //  minPt = octomap::point3d(minX, minY, minZ);
  //  maxPt = octomap::point3d(maxX, maxY, maxZ);

  octomap::OcTreeKey paddedMaxKey;
  if (!m_octree->coordToKeyChecked(minPt, m_maxTreeDepth, m_paddedMinKey)){
    ROS_ERROR("Could not create padded min OcTree key at %f %f %f", minPt.x(), minPt.y(), minPt.z());
    return;
  }
  if (!m_octree->coordToKeyChecked(maxPt, m_maxTreeDepth, paddedMaxKey)){
    ROS_ERROR("Could not create padded max OcTree key at %f %f %f", maxPt.x(), maxPt.y(), maxPt.z());
    return;
  }

  ROS_DEBUG("Padded MinKey: %d %d %d / padded MaxKey: %d %d %d", m_paddedMinKey[0], m_paddedMinKey[1], m_paddedMinKey[2], paddedMaxKey[0], paddedMaxKey[1], paddedMaxKey[2]);
  assert(paddedMaxKey[0] >= maxKey[0] && paddedMaxKey[1] >= maxKey[1]);

  m_multires2DScale = 1 << (m_treeDepth - m_maxTreeDepth);
  m_gridmap->info.width = (paddedMaxKey[0] - m_paddedMinKey[0])/m_multires2DScale +1;
  m_gridmap->info.height = (paddedMaxKey[1] - m_paddedMinKey[1])/m_multires2DScale +1;

  int mapOriginX = minKey[0] - m_paddedMinKey[0];
  int mapOriginY = minKey[1] - m_paddedMinKey[1];
  assert(mapOriginX >= 0 && mapOriginY >= 0);

  // might not exactly be min / max of octree:
  octomap::point3d origin = m_octree->keyToCoord(m_paddedMinKey, m_treeDepth);
  double gridRes = m_octree->getNodeSize(m_maxTreeDepth);
  m_projectCompleteMap = (!m_incrementalUpdate || (std::abs(gridRes-m_gridmap->info.resolution) > 1e-6));
  m_gridmap->info.resolution = gridRes;
  m_gridmap->info.origin.position.x = origin.x() - gridRes*0.5;
  m_gridmap->info.origin.position.y = origin.y() - gridRes*0.5;
  if (m_maxTreeDepth != m_treeDepth){
    m_gridmap->info.origin.position.x -= m_res/2.0;
    m_gridmap->info.origin.position.y -= m_res/2.0;
  }

  // workaround for  multires. projection not working properly for inner nodes:
  // force re-building complete map
  if (m_maxTreeDepth < m_treeDepth)
    m_projectCompleteMap = true;


  if(m_projectCompleteMap){
    ROS_DEBUG("Rebuilding complete 2D map");
    m_gridmap->data.clear();
    // init to unknown:
    m_gridmap->data.resize(m_gridmap->info.width * m_gridmap->info.height, -1);

  } else {

    if (mapChanged(oldMapInfo, m_gridmap->info)){
      ROS_DEBUG("2D grid map size changed to %dx%d", m_gridmap->info.width, m_gridmap->info.height);
      adjustMapData(m_gridmap, oldMapInfo);
    }

    size_t mapUpdateBBXMinX = std::max(0, (int(m_updateBBXMin[0]) - int(m_paddedMinKey[0]))/int(m_multires2DScale));
    size_t mapUpdateBBXMinY = std::max(0, (int(m_updateBBXMin[1]) - int(m_paddedMinKey[1]))/int(m_multires2DScale));
    size_t mapUpdateBBXMaxX = std::min(int(m_gridmap->info.width-1), (int(m_updateBBXMax[0]) - int(m_paddedMinKey[0]))/int(m_multires2DScale));
    size_t mapUpdateBBXMaxY = std::min(int(m_gridmap->info.height-1), (int(m_updateBBXMax[1]) - int(m_paddedMinKey[1]))/int(m_multires2DScale));

    assert(mapUpdateBBXMaxX > mapUpdateBBXMinX);
    assert(mapUpdateBBXMaxY > mapUpdateBBXMinY);

    size_t numCols = mapUpdateBBXMaxX-mapUpdateBBXMinX +1;

    // test for max idx:
    uint max_idx = m_gridmap->info.width*mapUpdateBBXMaxY + mapUpdateBBXMaxX;
    if (max_idx  >= m_gridmap->data.size())
      ROS_ERROR("BBX index not valid: %d (max index %zu for size %d x %d) update-BBX is: [%zu %zu]-[%zu %zu]", max_idx, m_gridmap->data.size(), m_gridmap->info.width, m_gridmap->info.height, mapUpdateBBXMinX, mapUpdateBBXMinY, mapUpdateBBXMaxX, mapUpdateBBXMaxY);

    // reset proj. 2D map in bounding box:
    for (unsigned int j = mapUpdateBBXMinY; j <= mapUpdateBBXMaxY; ++j){
      std::fill_n(m_gridmap->data.begin() + m_gridmap->info.width*j+mapUpdateBBXMinX,
                  numCols, -1);
    }
  }
}

void Volumetric_Map::handleOccupiedNode(const OcTreeT::iterator& it){

  if (m_projectCompleteMap){
    update2DMap(it, true);
  }
}

void Volumetric_Map::handleFreeNode(const OcTreeT::iterator& it){

  if (m_projectCompleteMap){
    update2DMap(it, false);
  }
}

void Volumetric_Map::handleOccupiedNodeInBBX(const OcTreeT::iterator& it){

  if  (!m_projectCompleteMap){
    update2DMap(it, true);
  }
}

void Volumetric_Map::handleFreeNodeInBBX(const OcTreeT::iterator& it){

  if (!m_projectCompleteMap){
    update2DMap(it, false);
  }
}

void Volumetric_Map::update2DMap(const OcTreeT::iterator& it, bool occupied)
{

  // update 2D map (occupied always overrides):

  if (it.getDepth() == m_maxTreeDepth){
    unsigned idx = mapIdx(it.getKey());
    if (occupied)
      m_gridmap->data[mapIdx(it.getKey())] = 100;
    else if (m_gridmap->data[idx] == -1){
      m_gridmap->data[idx] = 0;
    }

  } else{
    int intSize = 1 << (m_maxTreeDepth - it.getDepth());
    octomap::OcTreeKey minKey=it.getIndexKey();
    for(int dx=0; dx < intSize; dx++){
      int i = (minKey[0]+dx - m_paddedMinKey[0])/m_multires2DScale;
      for(int dy=0; dy < intSize; dy++){
        unsigned idx = mapIdx(i, (minKey[1]+dy - m_paddedMinKey[1])/m_multires2DScale);
        if (occupied)
          m_gridmap->data[idx] = 100;
        else if (m_gridmap->data[idx] == -1){
          m_gridmap->data[idx] = 0;
        }
      }
    }
  }
}

int Volumetric_Map::Get2DMapValue(const octomap::OcTreeKey &key_)
{

  if (m_octree->getTreeDepth() == m_maxTreeDepth)
    return m_gridmap->data[mapIdx(key_)];

  std::cout << "--reject--" << std::endl;

  return -2; // value not found in case the map depth value changed
}


void Volumetric_Map::adjustMapData(nav_msgs::OccupancyGridPtr map, const nav_msgs::MapMetaData& oldMapInfo) const
{
  if (map->info.resolution != oldMapInfo.resolution){
    ROS_ERROR("Resolution of map changed, cannot be adjusted");
    return;
  }
  int i_off = int((oldMapInfo.origin.position.x - map->info.origin.position.x)/map->info.resolution +0.5);
  int j_off = int((oldMapInfo.origin.position.y - map->info.origin.position.y)/map->info.resolution +0.5);

  if (i_off < 0 || j_off < 0
      || oldMapInfo.width  + i_off > map->info.width
      || oldMapInfo.height + j_off > map->info.height)
  {
    ROS_ERROR("New 2D map does not contain old map area, this case is not implemented");
    return;
  }

  nav_msgs::OccupancyGrid::_data_type oldMapData = map->data;

  map->data.clear();
  // init to unknown:
  map->data.resize(map->info.width * map->info.height, -1);

  nav_msgs::OccupancyGrid::_data_type::iterator fromStart, fromEnd, toStart;

  for (int j =0; j < int(oldMapInfo.height); ++j ){
    // copy chunks, row by row:
    fromStart = oldMapData.begin() + j*oldMapInfo.width;
    fromEnd = fromStart + oldMapInfo.width;
    toStart = map->data.begin() + ((j+j_off)*(m_gridmap->info.width) + i_off);
    copy(fromStart, fromEnd, toStart);
  }
}

void Volumetric_Map::Publish2DOccupancyMap(void)
{
  if (m_mapPub.getNumSubscribers()>0)
    if (m_publish2DMap)
      m_mapPub.publish(*m_gridmap);
}

void Volumetric_Map::SetCostMapRos(costmap_2d::Costmap2DROS *costmapR_)
{
  costmap_ros_=costmapR_;
}

void Volumetric_Map::GetActiveOctomapSize(double &x_size, double &y_size)
{
  costmap_=costmap_ros_->getCostmap();

  x_size= costmap_->getSizeInMetersX();
  y_size= costmap_->getSizeInMetersY();
}
void Volumetric_Map::GetActiveOrigin(double &x_origin, double &y_origin)
{
  costmap_=costmap_ros_->getCostmap();
  x_origin = costmap_->getOriginX() + costmap_->getSizeInMetersX()/2;
  y_origin = costmap_->getOriginY() + costmap_->getSizeInMetersY()/2;
}

void Volumetric_Map::GetPointCloud()
{
  ros::Time delay= ros::Time::now();
  while ((ros::Time::now()- delay)<ros::Duration(0.3))
  {
    ros::spinOnce();
    ros::Rate(5).sleep();
  }
  count_pointCloud=0;
  accept_pointCloud=true;
}

bool Volumetric_Map::GetPointCloudDone()
{
  // caputure point cloud for a number of times equal "num_recieved_pointCloud"
  if (count_pointCloud>=num_recieved_pointCloud){
    accept_pointCloud=false;
    return true;
  }
  return false;
}

void Volumetric_Map::CheckTFConnection()
{
  // TF failed some time at the start up, check until it succeed.
  tf::TransformListener tf_listener;
  tf::StampedTransform transform;
  std::string camera_link;
  ros::param::param<std::string>("~camera_optical_frame",camera_link, "/front_cam_depth_optical_frame");
  std::cout << "camera_link" << camera_link << std::endl;
  while(true)
  {
    try
    {
      tf_listener.lookupTransform(camera_link, "/world", ros::Time(0), transform);
      break; // Success, break out of the loop
    }
    catch (tf2::LookupException ex){
      ROS_INFO_THROTTLE(1,"[TEST_NBV] Waiting for Transformation %s",ex.what());
      ros::Duration(0.1).sleep();
    }
    catch (tf2::ExtrapolationException ex){
      ROS_INFO_THROTTLE(1,"[TEST_NBV] Waiting for Transformation %s",ex.what());
      ros::Duration(0.1).sleep();
    }
    catch (tf2::ConnectivityException ex){
      ROS_INFO_THROTTLE(1,"[TEST_NBV] Waiting for Transformation %s",ex.what());
      ros::Duration(0.1).sleep();
    }
  }
}

void Volumetric_Map::Stop()
{
  stop=true;
}
