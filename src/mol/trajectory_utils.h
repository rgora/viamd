#pragma once

#include <mol/trajectory.h>

// Reads the header info of a trajectory and allocates space for it
bool load_and_allocate_trajectory(Trajectory* traj, CString path);

// Reads the actual trajectory position information... Necessary?
bool read_trajectory_data(Trajectory* traj);

bool read_next_trajectory_frame(Trajectory* traj);
bool all_trajectory_frames_read(Trajectory* traj);
bool close_file_handle(Trajectory* traj);

void copy_trajectory_frame(TrajectoryFrame* dst, const Trajectory& src_traj, int frame_index);
void copy_trajectory_positions(Array<vec3> dst_array, const Trajectory& traj, int frame_index);
void read_trajectory_box_vectors(vec3 box_vectors[3], const Trajectory& traj, int frame_index);

TrajectoryFrame get_trajectory_frame(const Trajectory& traj, int frame_index);
Array<vec3> get_trajectory_positions(const Trajectory& traj, int frame_index);