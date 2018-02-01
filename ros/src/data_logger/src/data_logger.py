"""
Copyright (c) 2017, The Regents of the University of California (Regents).
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

   3. Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

Please contact the author(s) of this library if you have any questions.
Authors: David Fridovich-Keil   ( dfk@eecs.berkeley.edu )
"""

################################################################################
#
# Class to listen for trajectory requests, human poses, and robot poses,
# compute metrics, and dump to disk.
#
################################################################################

import rospy
import geometry_msgs.msg
import meta_planner_msgs.msg

import numpy as np

class DataLogger(object):
    def __init__(self):
        self._intialized = False

        # Logged data.
        self._traj_times = []
        self._min_collision_times = []
        self._best_collision_time = float("inf")

        # Start time and current goal of current trajectory.
        self._start_time = None
        self._current_goal = None

        # Human position and velocity, with time of last update.
        self._human_position = None
        self._human_velocity = None
        self._human_time = None

        # Robot position.
        self._robot_position = None

    # Initialization and loading parameters.
    def Initialize(self):
        self._name = rospy.get_name() + "/data_logger"

        # Load parameters.
        if not self.LoadParameters():
            rospy.logerr("%s: Error loading parameters.", self._name)
            return False

        # Register callbacks.
        if not self.RegisterCallbacks():
            rospy.logerr("%s: Error registering callbacks.", self._name)
            return False

        self._initialized = True
        return True

    def LoadParameters(self):
        # Get the timer interval.
        if not rospy.has_param("~time_step"):
            return False
        self._time_step = rospy.get_param("~time_step")

        # File name to save in.
        if not rospy.has_param("~file_name"):
            return False
        self._file_name = rospy.get_param("~file_name")

        # Topics.
        if not rospy.has_param("~topics/human"):
            return False
        self._human_topic = rospy.get_param("~topics/human")

        if not rospy.has_param("~topics/robot"):
            return False
        self._robot_topic = rospy.get_param("~topics/robot")

        if not rospy.has_param("~topics/traj_request"):
            return False
        self._traj_request_topic = rospy.get_param("~topics/traj_request")

        # Decay factor for human velocity estimation.
        # Values closer to 1 forget the past faster than those near 0.
        if not rospy.has_param("~velocity_decay"):
            return False
        self._velocity_decay = rospy.get_param("~velocity_decay")

        # Maximum angle between human and robot to calculate time to collision.
        if not rospy.has_param("~max_angle"):
            return False
        self._max_angle = rospy.get_param("~max_angle")

        return True

    def RegisterCallbacks(self):
        # Subscribers.
        self._human_sub = rospy.Subscriber(self._human_topic,
                                           geometry_msgs.msg.TransformStamped,
                                           self.HumanCallback)

        self._robot_sub = rospy.Subscriber(self._robot_topic,
                                           geometry_msgs.msg.TransformStamped,
                                           self.RobotCallback)

        self._traj_request_sub = rospy.Subscriber(self._traj_request_topic,
                                                  meta_planner_msgs.msg.TrajectoryRequest,
                                                  self.TrajRequestCallback)

        # Timer.
        self._timer = rospy.Timer(rospy.Duration(self._time_step),
                                  self.TimerCallback)

        return True

    # Update robot position.
    def RobotCallback(self, msg):
        if not self._initialized:
            return

        # Unpack msg.
        self._robot_time = msg.header.stamp
        self._robot_position = np.array([msg.transform.translation.x,
                                         msg.transform.translation.y,
                                         msg.transform.translation.z])

    # Update human position and velocity.
    def HumanCallback(self, msg):
        if not self._initialized:
            return

        # Unpack msg.
        position = np.array([msg.transform.translation.x,
                             msg.transform.translation.y,
                             msg.transform.translation.z])

        if self._human_position is None:
            self._human_position = position
            self._human_velocity = np.array([0.0, 0.0, 0.0])
            self._human_time = msg.header.stamp
        else:
            dt = (msg.header.stamp - self._human_time).to_sec()
            velocity = (position - self._human_position) / dt

            self._human_velocity = (velocity * self._velocity_decay +
                                    self._human_velocity * (1.0 - self._velocity_decay))
            self._human_position = position
            self._human_time = msg.header.stamp

    # Trajectory request callback.
    def TrajectoryRequestCallback(self, msg):
        if not self._initialized:
            return

        right_now = rospy.Time.now()

        # Catch the first one.
        if self._start_time is None:
            self._start_time = right_now
            self._current_goal = np.array([msg.goal_position.x,
                                           msg.goal_position.y,
                                           msg.goal_position.z])
            return

        # Check if we've reached the goal. This will be true if the
        # msg goal is not the same as our current goal.
        msg_goal = np.array([msg.goal_position.x,
                             msg.goal_position.y,
                             msg.goal_position.z])
        if np.linalg.norm(msg_goal - self._goal_position) > 1e-4:
            self._traj_times.append((right_now - self._start_time).to_sec())
            self._min_collision_times.append(self._best_collision_time)

            # Update goal and start time for next trajectory.
            self._goal_position = msg_goal
            self._start_time = rospy.Time.now()

            # Update best collision time.
            self._best_collision_time = float("inf")

    # Timer callback.
    def TimerCallback(self, event):
        if not self._initialized:
            return

        if self.Angle() < self._max_angle:
            self._best_collision_time = min(self._best_collision_time,
                                            self.CollisionTime())

    # Compute the angle between human velocity and human-to-robot direction.
    def Angle(self):
        if self._human_position is None or self._robot_position is None:
            return float("inf")

        direction = self._human_position - self._robot_position
        return np.arccos(np.dot(direction, self._human_velocity) /
                         (np.linalg.norm(self._human_velocity) *
                          np.linalg.norm(direction)))

    # Compute human's time to collision with robot's plane.
    def CollisionTime(self):
        if self._human_position is None or self._robot_position is None:
            return float("inf")

        direction = self._human_position - self._robot_position
        return np.linalg.norm(direction)**2 / \
            np.dot(direction, self._human_velocity)

    # Save to disk.
    def Save(self):
        num_samples = min(len(self._traj_times), len(self._min_collision_times))

        if (len(self._traj_times) != len(self._min_collision_times)):
            rospy.logwarn("%s: Number of trajectories and collision times do not match.",
                          self._name)

        # Columns will be as follows: min_collision_time | traj_time.
        table = np.zeros((num_samples, 2))
        for ii in range(num_samples):
            table[ii, 0] = self._min_collision_times[ii]
            table[ii, 1] = self._traj_times[ii]

        # Save.
        np.savetxt(self._file_name, table)
        rospy.loginfo("%s: Successfully saved data to disk.", self._name)