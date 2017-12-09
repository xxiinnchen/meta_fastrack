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
// Classical RRT in 3D, but where collision checks are time-dependent.
// Inherits from the Planner base class.
//
///////////////////////////////////////////////////////////////////////////////

#include <meta_planner/time_varying_rrt.h>

namespace meta {

// Factory method.
TimeVaryingRrt::Ptr TimeVaryingRrt::
Create(ValueFunctionId incoming_value, ValueFunctionId outgoing_value,
       const Box::ConstPtr& space, const Dynamics::ConstPtr& dynamics) {
  TimeVaryingRrt::Ptr ptr(new TimeVaryingRrt(
    incoming_value, outgoing_value, space, dynamics));
  return ptr;
}

// Derived classes must plan trajectories between two points.
// Budget is the time the planner is allowed to take during planning.
Trajectory::Ptr TimeVaryingRrt::
Plan(const Vector3d& start, const Vector3d& stop,
     double start_time, double budget) const {
  // Check that both start and stop are in bounds.
  if (!space_->IsValid(start, incoming_value_, outgoing_value_)) {
    ROS_WARN_THROTTLE(1.0, "TimeVaryingRrt: Start point was in collision or out of bounds.");
    return nullptr;
  }

  if (!space_->IsValid(stop, incoming_value_, outgoing_value_)) {
    ROS_WARN_THROTTLE(1.0, "TimeVaryingRrt: Stop point was in collision or out of bounds.");
    return nullptr;
  }

  // Root the RRT at the start point.
  const Node::ConstPtr root = Node::Create(start, nullptr, start_time);
  kdtree_.Insert(root);

  // Loop until our time budget has expired.
  const ros::Time begin = ros::Time::now();
  Node::ConstPtr terminus = nullptr;

  while ((ros::Time::now() - begin).toSec() < budget) {
    // Sample a new point.
    const Vector3d sample = space_.Sample();

    // Find the nearest neighbor in our existing kdtree.
    const size_t kNumNeigbhbors = 1;
    const std::vector<Node::ConstPtr> neighbors =
      kdtree_.KnnSearch(sample, kNumNeigbhbors);

#ifdef ENABLE_DEBUG_MESSAGES
    if (neighbors.size() != kNumNeigbhbors) {
      // Should never get here.
      ROS_ERROR("TimeVaryingRrt: KnnSearch found the wrong number of neighbors.");
      return nullptr;
    }
#endif

    // Compute the time at which we would get to the sample point.
    const double sample_time =
      neighbors[0]->time_ + BestPossibleTime(neighbors[0]->point_, sample);

    // Try to connect the sample to the nearest neighbor.
    if (!CollisionCheck(neighbors[0]->point_, sample,
                        neighbors[0]->time_, sample_time))
      continue;

    // Insert this point into the kdtree.
    const Node::ConstPtr sample_node =
      Node::Create(sample, neigbors[0], sample_time);
    kdtree_.Insert(sample_node);

    // Try to connect the sample to the goal.
    const double stop_time = sample_time + BestPossibleTime(sample, stop);
    if (!CollisionCheck(sample, stop, sample_time, stop_time))
      continue;

    // TODO! @DFK deal with terminus udpates. Reject samples a la informative RRT.
    // Reject far away samples, etc.
  }

  return nullptr;
}

// Collision check a line segment between the two points with the given
// initial start time. Returns true if the path is collision free and
// false otherwise.
bool TimeVaryingRrt::CollisionCheck(
  const Vector3d& start, const Vector3d& stop, double start_time) const {
  // TODO!
  return false;
}


} //\namespace meta
