#ifndef VIEW_EVALUATOR_WEIGHTED_MAX_H
#define VIEW_EVALUATOR_WEIGHTED_MAX_H

#include <victim_localization/view_evaluator_base.h>

class view_evaluator_weighted_max : public view_evaluator_base
{
public:
  view_evaluator_weighted_max();

   double calculateUtility(geometry_msgs::Pose p, Victim_Map_Base *mapping_module);
   void calculateIGwithMax(geometry_msgs::Pose p, Victim_Map_Base *mapping_module, double &IG, double &Max);
   void calculateWirelessIGwithMax(geometry_msgs::Pose p, Victim_Map_Base *mapping_module, double &IG, double &Max);
   void evaluate();
   void evaluateWireless();
   void evaluateCombined();


   std::string getMethodName();
   std::vector<geometry_msgs::Pose> Info_poses;
   std::vector<double> Info_View_utilities;
   std::vector<double> Info_View_Max;
   std::vector<double> Info_WirelessDiection;


   double exploration_weight;
   double victim_finding_weight;
   double dist_weight;


   // For combine Map
   std::vector<double> Info_View_utilities_DL;
   std::vector<double> Info_View_utilities_Thermal;
   std::vector<double> Info_View_utilities_Wireless;

   std::vector<double> Info_View_Max_DL;
   std::vector<double> Info_View_Max_Thermal;
   std::vector<double> Info_View_Max_Wireless;

private:
  double min_belief;
  double max_belief;

};

#endif // VIEW_EVALUATOR_WEIGHTED_MAX_H
