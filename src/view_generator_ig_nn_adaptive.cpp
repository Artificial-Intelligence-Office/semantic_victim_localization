#include "victim_localization/view_generator_ig_nn_adaptive.h"

view_generator_ig_nn_adaptive::view_generator_ig_nn_adaptive():
view_generator_IG(),
scale_factor_(1),
do_adaptive_generation(true),
new_cell_status(false),
new_cell_status_stop(false)
{
  ros::param::param<int>("~view_generator_nn_adaptive_local_minima_iterations", minima_iterations_, 5);
  ros::param::param<double>("~view_generator_nn_adaptive_utility_threshold", minima_threshold_, 3.0);
  ros::param::param<double>("~view_generator_nn_adaptive_scale_multiplier", scale_multiplier_, 2.0);
  ros::param::param<double>("~view_generator_nn_adaptive_entropy_max", entropy_max, 0.1);

  ros::param::param<double>("~view_generator_nn_adaptive_new_cell_percentage", new_cell_percentage_threshold, 0.1);
  ros::param::param<double>("~view_generator_nn_adaptive_new_cell_percentage_respawn", new_cell_percentage_threshold_respawn, 0.5);
  ros::param::param<double>("~view_generator_nn_adaptive_new_cell_percentage_N", new_cell_percentage_Iteration, 3);

  generator_type=Generator::NN_Generator;
}

bool view_generator_ig_nn_adaptive::isStuckInLocalMinima()
{
  if (minima_iterations_ > nbv_history_->iteration)
    return false;

  if (nbv_history_->max_prob > 0.51)
      return false;

  // See if the last few moves are repeating
//  if (nbv_history_->isRepeatingMotions(4))
//    return true;

  // Find max entropy change in the past few iterations
  //float utility = nbv_history_->getMaxUtility(minima_iterations_);
  // std::cout << "maximum_entropy_change_is: " << utility << std::endl;
  //if (utility < minima_threshold_)
    //return true;

  std::cout << "entropy_max" << std::endl;

  if (new_cell_status_stop){
     nbv_history_->CheckPercentageofUnVisitedCellsIsLow(new_cell_percentage_Iteration,new_cell_percentage_threshold,current_percentage);
     if (current_percentage>new_cell_percentage_threshold_respawn){
     new_cell_status_stop=false;
     return false;
     }
    }

  if (!new_cell_status_stop)
  {
  if(nbv_history_->CheckPercentageofUnVisitedCellsIsLow(new_cell_percentage_Iteration,new_cell_percentage_threshold,current_percentage)){
   new_cell_status=true;
   return true;
  }
  }

  return false;
}

void view_generator_ig_nn_adaptive::generateViews()
{
  if (isStuckInLocalMinima())
  {
    scale_factor_*= scale_multiplier_;
    std::cout << "[ViewGeneratorNNAdaptive]: " << cc.yellow << "Local minima detected. Increasing scale factor to " << scale_factor_ << "\n" << cc.reset;

    if (scale_factor_ > 16)
    {
      std::cout << "[ViewGeneratorNNAdaptive]: " << cc.red << "Warning: Scale factor very large: Resetting" << scale_factor_ << "\n" << cc.reset;
      scale_factor_ = 1;
    }
  }
  else
  {
    scale_factor_ = 1;
  }

  // Scale up sampling resolution
  double backup_start_x_ = start_x_;
  double backup_start_y_ = start_y_;
  double backup_end_x_ = end_x_;
  double backup_end_y_ = end_y_;

  start_x_ *= scale_factor_;
  start_y_ *= scale_factor_;
  end_x_ *= scale_factor_;
  end_y_ *= scale_factor_;

  // for scale_factor=1, generate viewpoint using NN generator
  if (scale_factor_==1)
  {
    nav_type = 0; // set navigation type as straight line for adaptive_nn_view_generator
    generator_type=Generator::NN_Generator;
    view_generator_IG::generateViews(true);
  }

  // for scale_factor>1, generate viewpoint using adaptive grid
  else
  {
      generator_type=Generator::NN_Adaptive_Generator;
      nav_type = 1; // set navigation type as reactive planner for adaptive_nn_view_generator
      view_generator_IG::generateViews(true); // do not sample in the current pose to escape local minimum
  }

  // Return start and end  to normal
    start_x_= backup_start_x_;
    start_y_= backup_start_y_;
    end_x_ = backup_end_x_;
    end_y_=  backup_end_y_;
}

bool view_generator_ig_nn_adaptive::IsPreviouslyCheckedSamples(double i_x,double i_y)
{
  if ((i_x>=(-res_x_/scale_multiplier_)) && (i_x<=(res_x_/scale_multiplier_)))
    if ((i_y>=(-res_y_/scale_multiplier_)) && (i_y<=(res_y_/scale_multiplier_)))
       return true;

  return false;
}

std::string view_generator_ig_nn_adaptive::getMethodName()
{
  return "NN Adaptive";
}
