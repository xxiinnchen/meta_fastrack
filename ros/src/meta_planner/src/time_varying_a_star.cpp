/*
 * Copyright (c) 2017, The Regents of the University of California (Regents).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *    3. Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Please contact the author(s) of this library if you have any questions.
 * Authors: David Fridovich-Keil   ( dfk@eecs.berkeley.edu )
 */

///////////////////////////////////////////////////////////////////////////////
//
// Classical A* in 3D, but where collision checks are time-dependent.
// Inherits from the Planner base class.
//
///////////////////////////////////////////////////////////////////////////////

#include <meta_planner/time_varying_a_star.h>

#include <algorithm>
#include <vector>

namespace meta {

// Factory method.
TimeVaryingAStar::Ptr TimeVaryingAStar::
Create(ValueFunctionId incoming_value,
       ValueFunctionId outgoing_value,
       const ProbabilisticBox::ConstPtr& space,
       const Dynamics::ConstPtr& dynamics,
       double grid_resolution,
       double collision_check_resolution) {
  TimeVaryingAStar::Ptr ptr(new TimeVaryingAStar(
    incoming_value, outgoing_value, space, dynamics,
    grid_resolution, collision_check_resolution));
  return ptr;
}

// Derived classes must plan trajectories between two points.
// Budget is the time the planner is allowed to take during planning.
Trajectory::Ptr TimeVaryingAStar::
Plan(const Vector3d& start, const Vector3d& stop,
     double start_time, double budget) const {

	const ros::Time plan_start_time = ros::Time::now();
  const double kStayPutTime = 1.0;

  // Make a multiset.
  std::multiset<Node::Ptr, typename Node::NodeComparitor> open;

  // Make a hash set for the 'closed list'.
  std::unordered_set<Node::Ptr, typename Node::NodeHasher> closed;

  // Initialize the priority queue.
  const double start_cost = 0.0;
  const double start_heuristic = ComputeHeuristic(start, stop);
  const Node::Ptr start_node =
    Node::Create(start, nullptr, start_time, start_cost, start_heuristic);

  open.insert(start_node);

  // Main loop - repeatedly expand the top priority node and
  // insert neighbors that are not already in the closed list.
  while (true) {
		if ((ros::Time::now() - plan_start_time).toSec() > budget)
			return nullptr;

		if (open.empty()){
			ROS_ERROR("%s: Open list is empty.", name_.c_str());
			return nullptr;
		}

    const Node::Ptr next = *open.begin();
    std::printf("point: [%5.3f, %5.3f, %5.3f]\n", 
      next->point_[0], next->point_[1], next->point_[2]);
    std::printf("prob: %5.3f\n", next->collision_prob_);
    std::printf("time: %f\n", next->time_);

    open.erase(open.begin());

    // Check if this guy is the goal.
    if (std::abs(next->point_(0) - stop(0)) < grid_resolution_/2.0 &&
				std::abs(next->point_(1) - stop(1)) < grid_resolution_/2.0 &&
				std::abs(next->point_(2) - stop(2)) < grid_resolution_/2.0){
			const Node::ConstPtr parent_node = (next->parent_ == nullptr) ? 
				next : next->parent_;

			// Have to connect the goal point to the last sampled grid point.
      const double best_time = BestPossibleTime(parent_node->point_, stop);
			const double terminus_time = parent_node->time_ + best_time;
      const double terminus_cost = 
        ComputeCostToCome(parent_node, stop, best_time);
      const double terminus_heuristic = 0.0;

			const Node::Ptr terminus = 
				Node::Create(stop, parent_node, terminus_time, terminus_cost, 
                      terminus_heuristic);
      return GenerateTrajectory(terminus);
		}

    // Add this to the closed list.
    closed.insert(next);

    // Expand and add to the list.
    for (const Vector3d& neighbor : Neighbors(next->point_)) {
      // Compute the time at which we'll reach this neighbor.
      const double best_neigh_time = (neighbor.isApprox(next->point_, 1e-8)) ? 
        kStayPutTime : BestPossibleTime(next->point_, neighbor);

      const double neighbor_time = next->time_ + best_neigh_time;
      
      // Compute cost to get to the neighbor.
      const double neighbor_cost = 
        ComputeCostToCome(next, neighbor, best_neigh_time);

      // Compute heuristic from neighbor to stop.
      const double neighbor_heuristic = ComputeHeuristic(neighbor, stop);

      // Discard if this is on the closed list.
      const Node::Ptr neighbor_node =
        Node::Create(neighbor, next, neighbor_time, neighbor_cost, neighbor_heuristic);
      if (closed.count(neighbor_node) > 0)
        continue;

      // Collision check this line segment (and store the collision probability)
      if (!CollisionCheck(next->point_, neighbor, next->time_, 
            neighbor_time, neighbor_node->collision_prob_))
        continue;

      // Check if we're in the open set.
      auto match = open.find(neighbor_node);
      if (match != open.end()) {
        // We found a match.
        if (neighbor_node->priority_ < (*match)->priority_) {
          open.erase(match);
          open.insert(neighbor_node);
        }
      } else {
        open.insert(neighbor_node);
      }
    }
  }

}

// Returns the total cost to get to point.
double TimeVaryingAStar::ComputeCostToCome(const Node::ConstPtr& parent, 
  const Vector3d& point, double dt) const{

  if(parent == nullptr){
    ROS_ERROR("Parent shold never be null when computing cost to come!");
    return std::numeric_limits<double>::infinity();
  }

  if (dt < 0.0)
    dt = BestPossibleTime(parent->point_, point);  

  // Cost to get to the parent contains distance + time. Add to this
  // the distance from the parent to the current point and the time
  return parent->cost_to_come_ + (parent->point_ - point).norm(); //+ dt;
}

// Returns the heuristic for the point.
double TimeVaryingAStar::ComputeHeuristic(const Vector3d& point, 
  const Vector3d& stop) const{

  // This heuristic is the best possible distance + best possible time. 
  return BestPossibleTime(point, stop); //(point - stop).norm() + 
}


// Get the neighbors of the given point on the implicit grid.
// NOTE! Include the given point.
std::vector<Vector3d> TimeVaryingAStar::Neighbors(const Vector3d& point) const {
  std::vector<Vector3d> neighbors;

  // Add all the 27 neighbors (including the point itself).
  for (double x = point(0) - grid_resolution_;
       x < point(0) + grid_resolution_ + 1e-8;
       x += grid_resolution_) {
    for (double y = point(1) - grid_resolution_;
         y < point(1) + grid_resolution_ + 1e-8;
         y += grid_resolution_) {
      for (double z = point(2) - grid_resolution_;
           z < point(2) + grid_resolution_ + 1e-8;
           z += grid_resolution_) {
        neighbors.push_back(Vector3d(x, y, z));
      }
    }
  }

  return neighbors;
}

// Collision check a line segment between the two points with the given
// initial start time. Returns true if the path is collision free and
// false otherwise.
bool TimeVaryingAStar::CollisionCheck(const Vector3d& start, const Vector3d& stop,
                                    double start_time, double stop_time, 
                                    double& max_collision_prob) const {

	// Need to check if collision checking against yourself
	const bool same_pt = start.isApprox(stop, 1e-8);

  // Compute the unit vector pointing from start to stop.
  const Vector3d direction = (same_pt) ? Vector3d::Zero() : 
		static_cast<Vector3d>((stop - start) / (stop - start).norm());

  // Compute the dt between query points.
  const double dt = (same_pt) ? (stop_time-start_time)*0.1 : 
		(stop_time - start_time) * collision_check_resolution_ / 
		(stop - start).norm();

  double collision_prob = 0.0;
  // Start at the start point and walk until we get past the stop point.
  Vector3d query(start);
  for (double time = start_time; time < stop_time; time += dt) {
    const bool valid_pt = 
      space_->IsValid(query, incoming_value_, outgoing_value_, collision_prob, time);

    if (collision_prob > max_collision_prob)
      max_collision_prob = collision_prob;

    if (!valid_pt)
      return false;

    // Take a step.
    query += collision_check_resolution_ * direction;
  }

  return true;
}

// Walk backward from the given node to the root to create a Trajectory.
Trajectory::Ptr TimeVaryingAStar::GenerateTrajectory(
  const Node::ConstPtr& node) const {
  // Start with an empty list of positions and times.
  std::vector<Vector3d> positions;
  std::vector<double> times;

  std::cout << "Collision probabilities for generated trajectory:\n";
  // Populate these lists by walking backward, then reverse.
  for (Node::ConstPtr n = node; n != nullptr; n = n->parent_) {
    positions.push_back(n->point_);
    times.push_back(n->time_);
    std::printf("%5.3f, ", n->collision_prob_);
  }
  std::cout << std::endl;

  std::reverse(positions.begin(), positions.end());
  std::reverse(times.begin(), times.end());

  // Lift positions into states.
  const std::vector<VectorXd> states =
    dynamics_->LiftGeometricTrajectory(positions, times);

  // Create dummy list containing value function IDs.
  const std::vector<ValueFunctionId> values(states.size(), incoming_value_);

	ROS_INFO("Returning Trajectory of length %zu.", positions.size());

  // Create a trajectory.
  return Trajectory::Create(times, states, values, values);
}

} //\namespace meta
