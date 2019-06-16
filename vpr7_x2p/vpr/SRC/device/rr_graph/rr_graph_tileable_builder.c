/**********************************************************
 * MIT License
 *
 * Copyright (c) 2018 LNIS - The University of Utah
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ***********************************************************************/

/************************************************************************
 * Filename:    rr_graph_tileable_builder.c
 * Created by:   Xifan Tang
 * Change history:
 * +-------------------------------------+
 * |  Date       |    Author   | Notes
 * +-------------------------------------+
 * | 2019/06/11  |  Xifan Tang | Created 
 * +-------------------------------------+
 ***********************************************************************/
/************************************************************************
 *  This file contains a builder for the complex rr_graph data structure 
 *  Different from VPR rr_graph builders, this builder aims to create a 
 *  highly regular rr_graph, where each Connection Block (CB), Switch 
 *  Block (SB) is the same (except for those on the borders). Thus, the
 *  rr_graph is called tileable, which brings significant advantage in 
 *  producing large FPGA fabrics.
 ***********************************************************************/

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <vector>
#include "vpr_types.h"
#include "globals.h"
#include "vpr_utils.h"
#include "rr_graph_util.h"
#include "rr_graph.h"
#include "rr_graph2.h"
#include "route_common.h"
#include "fpga_x2p_types.h"
#include "rr_graph_tileable_builder.h"

#include "chan_node_details.h"
#include "device_coordinator.h"

/************************************************************************
 * Local function in the file 
 ***********************************************************************/

/************************************************************************
 * Generate the number of tracks for each types of routing segments
 * w.r.t. the frequency of each of segments and channel width
 * Note that if we dertermine the number of tracks per type using
 *     chan_width * segment_frequency / total_freq may cause 
 * The total track num may not match the chan_width, 
 * therefore, we assign tracks one by one until we meet the frequency requirement
 * In this way, we can assign the number of tracks with repect to frequency 
 ***********************************************************************/
static 
std::vector<size_t> get_num_tracks_per_seg_type(size_t chan_width, 
                                                std::vector<t_segment_inf> segment_inf, 
                                                bool use_full_seg_groups) {
  std::vector<size_t> result;
  std::vector<double> demand;
  /* Make sure a clean start */
  result.resize(segment_inf.size());
  demand.resize(segment_inf.size());

  /* Scale factor so we can divide by any length
   * and still use integers */
  /* Get the sum of frequency */
  size_t scale = 1;
  size_t freq_sum = 0;
  for (size_t iseg = 0; iseg < segment_inf.size(); ++iseg) {
    scale *= segment_inf[iseg].length;
    freq_sum += segment_inf[iseg].frequency;
  }
  size_t reduce = scale * freq_sum;

  /* Init assignments to 0 and set the demand values */
  /* Get the fraction of each segment type considering the frequency:
   * num_track_per_seg = chan_width * (freq_of_seg / sum_freq)
   */
  for (size_t iseg = 0; iseg < segment_inf.size(); ++iseg) {
    result[iseg] = 0;
    demand[iseg] = scale * chan_width * segment_inf[iseg].frequency;
    if (true == use_full_seg_groups) {
      demand[iseg] /= segment_inf[iseg].length;
    }
  }

  /* check if the sum of num_tracks, matches the chan_width */
  /* Keep assigning tracks until we use them up */
  size_t assigned = 0;
  size_t size = 0;
  size_t imax = 0;
  while (assigned < chan_width) {
    /* Find current maximum demand */
    double max = 0;
    for (size_t iseg = 0; iseg < segment_inf.size(); ++iseg) {
      if (demand[iseg] > max) {
        imax = iseg;
      }
      max = std::max(demand[iseg], max); 
    }

    /* Assign tracks to the type and reduce the types demand */
    size = (use_full_seg_groups ? segment_inf[imax].length : 1);
    demand[imax] -= reduce;
    result[imax] += size;
    assigned += size;
  }

  /* Undo last assignment if we were closer to goal without it */
  if ((assigned - chan_width) > (size / 2)) {
    result[imax] -= size;
  }

  return result;
} 

/************************************************************************
 * Build details of routing tracks in a channel 
 * The function will 
 * 1. Assign the segments for each routing channel,
 *    To be specific, for each routing track, we assign a routing segment.
 *    The assignment is subject to users' specifications, such as 
 *    a. length of each type of segment
 *    b. frequency of each type of segment.
 *    c. routing channel width
 *
 * 2. The starting point of each segment in the channel will be assigned
 *    For each segment group with same directionality (tracks have the same length),
 *    every L track will be a starting point (where L denotes the length of segments)
 *    In this case, if the number of tracks is not a multiple of L,
 *    indeed we may have some <L segments. This can be considered as a side effect.
 *    But still the rr_graph is tileable, which is the first concern!
 *
 *    Here is a quick example of Length-4 wires in a W=12 routing channel
 *    +---------------------------------+
 *    | Index | Direction | Start Point |
 *    +---------------------------------+
 *    |   0   | --------> |   Yes       |
 *    +---------------------------------+
 *    |   1   | <-------- |   Yes       |
 *    +---------------------------------+
 *    |   2   | --------> |   No        |
 *    +---------------------------------+
 *    |   3   | <-------- |   No        |
 *    +---------------------------------+
 *    |   4   | --------> |   No        |
 *    +---------------------------------+
 *    |   5   | <-------- |   No        |
 *    +---------------------------------+
 *    |   7   | --------> |   No        |
 *    +---------------------------------+
 *    |   8   | <-------- |   No        |
 *    +---------------------------------+
 *    |   9   | --------> |   Yes       |
 *    +---------------------------------+
 *    |   10  | <-------- |   Yes       |
 *    +---------------------------------+
 *    |   11  | --------> |   No        |
 *    +---------------------------------+
 *    |   12  | <-------- |   No        |
 *    +---------------------------------+
 *
 * 3. SPECIAL for fringes: TOP|RIGHT|BOTTOM|RIGHT
 *    if device_side is NUM_SIDES, we assume this channel does not locate on borders
 *    All segments will start and ends with no exception
 *
 * 4. IMPORTANT: we should be aware that channel width maybe different 
 *    in X-direction and Y-direction channels!!!
 *    So we will load segment details for different channels 
 ***********************************************************************/
static 
ChanNodeDetails build_unidir_chan_node_details(size_t chan_width, size_t max_seg_length,
                                               enum e_side device_side, 
                                               std::vector<t_segment_inf> segment_inf) {
  ChanNodeDetails chan_node_details;
  /* Correct the chan_width: it should be an even number */
  if (0 != chan_width % 2) {
    chan_width++; /* increment it to be even */
  }
  assert (0 == chan_width % 2);
  
  /* Reserve channel width */
  chan_node_details.reserve(chan_width);
  /* Return if zero width is forced */
  if (0 == chan_width) {
    return chan_node_details; 
  }

  /* Find the number of segments required by each group */
  std::vector<size_t> num_tracks = get_num_tracks_per_seg_type(chan_width/2, segment_inf, TRUE);  

  /* Add node to ChanNodeDetails */
  size_t cur_track = 0;
  for (size_t iseg = 0; iseg < segment_inf.size(); ++iseg) {
    /* segment length will be set to maxium segment length if this is a longwire */
    size_t seg_len = segment_inf[iseg].length;
    if (TRUE == segment_inf[iseg].longline) {
       seg_len = max_seg_length;
    } 
    for (size_t itrack = 0; itrack < num_tracks[iseg]; ++itrack) {
      bool seg_start = false;
      /* Every length of wire, we set a starting point */
      if (0 == itrack % seg_len) {
        seg_start = true;
      }
      /* Since this is a unidirectional routing architecture,
       * Add a pair of tracks, 1 INC_DIRECTION track and 1 DEC_DIRECTION track 
       */
      chan_node_details.add_track(cur_track, INC_DIRECTION, seg_len, seg_start, false);
      cur_track++;
      chan_node_details.add_track(cur_track, DEC_DIRECTION, seg_len, seg_start, false);
      cur_track++;
    }    
  }
  /* Check if all the tracks have been satisified */ 
  assert (cur_track == chan_width);

  /* If this is on the border of a device, segments should start */
  switch (device_side) {
  case TOP:
  case RIGHT:
    /* INC_DIRECTION should all end */
    chan_node_details.set_tracks_end(INC_DIRECTION);
    /* DEC_DIRECTION should all start */
    chan_node_details.set_tracks_start(DEC_DIRECTION);
    break;
  case BOTTOM:
  case LEFT:
    /* INC_DIRECTION should all start */
    chan_node_details.set_tracks_start(INC_DIRECTION);
    /* DEC_DIRECTION should all end */
    chan_node_details.set_tracks_end(DEC_DIRECTION);
    break;
  case NUM_SIDES:
    break;
  default:
    vpr_printf(TIO_MESSAGE_ERROR, 
               "(File:%s, [LINE%d]) Invalid device_side!\n", 
               __FILE__, __LINE__);
    exit(1);
  }

  return chan_node_details; 
}

/* Deteremine the side of a io grid */
static 
enum e_side determine_io_grid_pin_side(const DeviceCoordinator& device_size, 
                                       const DeviceCoordinator& grid_coordinator) {
  /* TOP side IO of FPGA */
  if (device_size.get_y() == grid_coordinator.get_y()) {
    return BOTTOM; /* Such I/O has only Bottom side pins */
  } else if (device_size.get_x() == grid_coordinator.get_x()) { /* RIGHT side IO of FPGA */
    return LEFT; /* Such I/O has only Left side pins */
  } else if (0 == grid_coordinator.get_y()) { /* BOTTOM side IO of FPGA */
    return TOP; /* Such I/O has only Top side pins */
  } else if (0 == grid_coordinator.get_x()) { /* LEFT side IO of FPGA */
    return RIGHT; /* Such I/O has only Right side pins */
  } else {
    vpr_printf(TIO_MESSAGE_ERROR, "(File:%s, [LINE%d])I/O Grid is in the center part of FPGA! Currently unsupported!\n",
               __FILE__, __LINE__);
    exit(1);
  }
}

/************************************************************************
 * Get a list of pin_index for a grid (either OPIN or IPIN)
 * For IO_TYPE, only one side will be used, we consider one side of pins 
 * For others, we consider all the sides  
 ***********************************************************************/
static 
std::vector<int> get_grid_side_pins(const t_grid_tile& cur_grid, enum e_pin_type pin_type, enum e_side pin_side, int pin_height) {
  std::vector<int> pin_list; 
  /* Make sure a clear start */
  pin_list.clear();

  for (int ipin = 0; ipin < cur_grid.type->num_pins; ++ipin) {
    if ( (1 == cur_grid.type->pinloc[pin_height][pin_side][ipin]) 
      && (pin_type == cur_grid.type->pin_class[ipin]) ) {
      pin_list.push_back(ipin);
    }
  }
  return pin_list;
}

/************************************************************************
 * Get the number of pins for a grid (either OPIN or IPIN)
 * For IO_TYPE, only one side will be used, we consider one side of pins 
 * For others, we consider all the sides  
 ***********************************************************************/
static 
size_t get_grid_num_pins(const t_grid_tile& cur_grid, enum e_pin_type pin_type, enum e_side io_side) {
  size_t num_pins = 0;
  Side io_side_manager(io_side);
  /* For IO_TYPE sides */
  for (size_t side = 0; side < NUM_SIDES; ++side) {
    Side side_manager(side);
    /* skip unwanted sides */
    if ( (IO_TYPE == cur_grid.type)
      && (side != io_side_manager.to_size_t()) ) { 
      continue;
    }
    /* Get pin list */
    for (int height = 0; height < cur_grid.type->height; ++height) {
      std::vector<int> pin_list = get_grid_side_pins(cur_grid, pin_type, side_manager.get_side(), height);
      num_pins += pin_list.size();
    } 
  }

  return num_pins;
}

/************************************************************************
 * Estimate the number of rr_nodes per category:
 * CHANX, CHANY, IPIN, OPIN, SOURCE, SINK 
 ***********************************************************************/
static 
std::vector<size_t> estimate_num_rr_nodes_per_type(const DeviceCoordinator& device_size,
                                                   std::vector<std::vector<t_grid_tile>> grids,
                                                   std::vector<size_t> chan_width,
                                                   std::vector<t_segment_inf> segment_infs) {
  std::vector<size_t> num_rr_nodes_per_type;
  /* reserve the vector: 
   * we have the follow type:
   * SOURCE = 0, SINK, IPIN, OPIN, CHANX, CHANY, INTRA_CLUSTER_EDGE, NUM_RR_TYPES
   * NUM_RR_TYPES and INTRA_CLUSTER_EDGE will be 0
   */
  num_rr_nodes_per_type.resize(NUM_RR_TYPES);
  /* Make sure a clean start */
  for (size_t i = 0; i < NUM_RR_TYPES; ++i) {
    num_rr_nodes_per_type[i] = 0;
  }

  /************************************************************************
   * 1. Search the grid and find the number OPINs and IPINs per grid
   *    Note that the number of SOURCE nodes are the same as OPINs
   *    and the number of SINK nodes are the same as IPINs
   ***********************************************************************/
  for (size_t ix = 0; ix < grids.size(); ++ix) {
    for (size_t iy = 0; iy < grids[ix].size(); ++iy) { 
      /* Skip EMPTY tiles */
      if (EMPTY_TYPE == grids[ix][iy].type) {
        continue;
      }
      /* Skip height>1 tiles (mostly heterogeneous blocks) */
      if (0 < grids[ix][iy].offset) {
        continue;
      }
      enum e_side io_side = NUM_SIDES;
      /* If this is the block on borders, we consider IO side */
      if (IO_TYPE == grid[ix][iy].type) {
        DeviceCoordinator io_device_size(device_size.get_x() - 1, device_size.get_y() - 1);
        DeviceCoordinator grid_coordinator(ix, iy);
        io_side = determine_io_grid_pin_side(device_size, grid_coordinator);
      }
      /* get the number of OPINs */
      num_rr_nodes_per_type[OPIN] += get_grid_num_pins(grids[ix][iy], DRIVER, io_side);
      /* get the number of IPINs */
      num_rr_nodes_per_type[IPIN] += get_grid_num_pins(grids[ix][iy], RECEIVER, io_side);
    }
  }
  /* SOURCE and SINK */
  num_rr_nodes_per_type[SOURCE] = num_rr_nodes_per_type[OPIN];
  num_rr_nodes_per_type[SINK]   = num_rr_nodes_per_type[IPIN];

  /************************************************************************
   * 2. Assign the segments for each routing channel,
   *    To be specific, for each routing track, we assign a routing segment.
   *    The assignment is subject to users' specifications, such as 
   *    a. length of each type of segment
   *    b. frequency of each type of segment.
   *    c. routing channel width
   *
   *    SPECIAL for fringes:
   *    All segments will start and ends with no exception
   *
   *    IMPORTANT: we should be aware that channel width maybe different 
   *    in X-direction and Y-direction channels!!!
   *    So we will load segment details for different channels 
   ***********************************************************************/
  /* For X-direction Channel */
  /* For LEFT side of FPGA */
  ChanNodeDetails left_chanx_details = build_unidir_chan_node_details(chan_width[0], device_size.get_x() - 2, LEFT, segment_infs); 
  for (size_t iy = 0; iy < device_size.get_y() - 1; ++iy) { 
    num_rr_nodes_per_type[CHANX] += left_chanx_details.get_num_starting_tracks();
  }
  /* For RIGHT side of FPGA */
  ChanNodeDetails right_chanx_details = build_unidir_chan_node_details(chan_width[0], device_size.get_x() - 2, RIGHT, segment_infs); 
  for (size_t iy = 0; iy < device_size.get_y() - 1; ++iy) { 
    num_rr_nodes_per_type[CHANX] += right_chanx_details.get_num_starting_tracks();
  }
  /* For core of FPGA */
   ChanNodeDetails core_chanx_details = build_unidir_chan_node_details(chan_width[1], device_size.get_x() - 2, NUM_SIDES, segment_infs); 
  for (size_t ix = 1; ix < grids.size() - 2; ++ix) {
    for (size_t iy = 1; iy < grids[ix].size() - 2; ++iy) { 
      num_rr_nodes_per_type[CHANX] += core_chanx_details.get_num_starting_tracks();
    }
  }

  /* For Y-direction Channel */
  /* For TOP side of FPGA */
  ChanNodeDetails top_chany_details = build_unidir_chan_node_details(chan_width[1], device_size.get_y() - 2, TOP, segment_infs); 
  for (size_t ix = 0; ix < device_size.get_x() - 1; ++ix) { 
    num_rr_nodes_per_type[CHANY] += top_chany_details.get_num_starting_tracks();
  }
  /* For BOTTOM side of FPGA */
  ChanNodeDetails bottom_chany_details = build_unidir_chan_node_details(chan_width[1], device_size.get_y() - 2, BOTTOM, segment_infs); 
  for (size_t ix = 0; ix < device_size.get_x() - 1; ++ix) { 
    num_rr_nodes_per_type[CHANY] += bottom_chany_details.get_num_starting_tracks();
  }
  /* For core of FPGA */
   ChanNodeDetails core_chany_details = build_unidir_chan_node_details(chan_width[1], device_size.get_y() - 2, NUM_SIDES, segment_infs); 
  for (size_t ix = 1; ix < grids.size() - 2; ++ix) {
    for (size_t iy = 1; iy < grids[ix].size() - 2; ++iy) { 
      num_rr_nodes_per_type[CHANY] += core_chany_details.get_num_starting_tracks();
    }
  }

  return num_rr_nodes_per_type;
}


/************************************************************************
 * Main function of this file
 * Builder for a detailed uni-directional tileable rr_graph
 * Global graph is not supported here, the VPR rr_graph generator can be used  
 * It follows the procedures to complete the rr_graph generation
 * 1. Assign the segments for each routing channel,
 *    To be specific, for each routing track, we assign a routing segment.
 *    The assignment is subject to users' specifications, such as 
 *    a. length of each type of segment
 *    b. frequency of each type of segment.
 *    c. routing channel width
 * 2. Estimate the number of nodes in the rr_graph
 *    This will estimate the number of 
 *    a. IPINs, input pins of each grid
 *    b. OPINs, output pins of each grid
 *    c. SOURCE, virtual node which drives OPINs
 *    d. SINK, virtual node which is connected to IPINs
 *    e. CHANX and CHANY, routing segments of each channel
 * 3. Create the connectivity of OPINs
 *    a. Evenly assign connections to OPINs to routing tracks
 *    b. the connection pattern should be same across the fabric
 * 4. Create the connectivity of IPINs 
 *    a. Evenly assign connections from routing tracks to IPINs
 *    b. the connection pattern should be same across the fabric
 * 5. Create the switch block patterns, 
 *    It is based on the type of switch block, the supported patterns are 
 *    a. Disjoint, which connects routing track (i)th from (i)th and (i)th routing segments
 *    b. Universal, which connects routing track (i)th from (i)th and (M-i)th routing segments
 *    c. Wilton, which rotates the connection of Disjoint by 1 track
 * 6. Allocate rr_graph, fill the node information
 *    For each node, fill
 *    a. basic information: coordinator(xlow, xhigh, ylow, yhigh), ptc_num
 *    b. edges (both incoming and outcoming)
 *    c. handle direct-connections
 * 7. Build fast look-up for the rr_graph 
 * 8. Allocate external data structures
 *    a. cost_index
 *    b. RC tree
 ***********************************************************************/
t_rr_graph build_tileable_unidir_rr_graph(INP int L_num_types,
    INP t_type_ptr types, INP int L_nx, INP int L_ny,
    INP struct s_grid_tile **L_grid, INP int chan_width,
    INP struct s_chan_width_dist *chan_capacity_inf,
    INP enum e_switch_block_type sb_type, INP int Fs, INP int num_seg_types,
    INP int num_switches, INP t_segment_inf * segment_inf,
    INP int global_route_switch, INP int delayless_switch,
    INP t_timing_inf timing_inf, INP int wire_to_ipin_switch,
    INP enum e_base_cost_type base_cost_type, INP t_direct_inf *directs, 
    INP int num_directs, INP boolean ignore_Fc_0, OUTP int *Warnings,
    /*Xifan TANG: Switch Segment Pattern Support*/
    INP int num_swseg_pattern, INP t_swseg_pattern_inf* swseg_patterns,
    INP boolean opin_to_cb_fast_edges, INP boolean opin_logic_eq_edges) { 
  /* Create an empty graph */
  t_rr_graph rr_graph; 
  rr_graph.rr_node_indices = NULL;
  rr_graph.rr_node = NULL;
  rr_graph.num_rr_nodes = 0;

  /* Reset warning flag */
  *Warnings = RR_GRAPH_NO_WARN;

  /* Create a matrix of grid */
  DeviceCoordinator device_size(L_nx + 2, L_ny + 2);
  std::vector< std::vector<t_grid_tile> > grids;
  /* reserve vector capacity to be memory efficient */
  grids.resize(L_nx + 2);
  for (int ix = 0; ix < (L_nx + 2); ++ix) {
    grids[ix].resize(L_ny + 2);
    for (int iy = 0; ix < (L_ny + 2); ++iy) {
      grid[ix][iy] = L_grid[ix][iy];
    }
  }
  /* Create a vector of channel width, we support X-direction and Y-direction has different W */
  std::vector<size_t> device_chan_width;
  device_chan_width.push_back(chan_width);
  device_chan_width.push_back(chan_width);

  /* Create a vector of segment_inf */
  std::vector<t_segment_inf> segment_infs;
  for (int iseg = 0; iseg < num_seg_types; ++iseg) {
    segment_infs.push_back(segment_inf[iseg]);
  }

  /************************************************************************
   * 2. Estimate the number of nodes in the rr_graph
   *    This will estimate the number of 
   *    a. IPINs, input pins of each grid
   *    b. OPINs, output pins of each grid
   *    c. SOURCE, virtual node which drives OPINs
   *    d. SINK, virtual node which is connected to IPINs
   *    e. CHANX and CHANY, routing segments of each channel
   ***********************************************************************/
  std::vector<size_t> num_rr_nodes_per_type = estimate_num_rr_nodes_per_type(device_size, grids, device_chan_width, segment_infs); 

  /************************************************************************
   * 3. Allocate the rr_nodes 
   ***********************************************************************/
  rr_graph.num_rr_nodes = 0;
  for (size_t i = 0; i < num_rr_nodes_per_type.size(); ++i) {
    rr_graph.num_rr_nodes += num_rr_nodes_per_type[i];
  }
  /* use calloc to initialize everything to be zero */
  rr_graph.rr_node = (t_rr_node*)my_calloc(rr_graph.num_rr_nodes, sizeof(t_rr_node));

  /************************************************************************
   * 4. Initialize the basic information of rr_nodes:
   *    coordinators: xlow, ylow, xhigh, yhigh, 
   *    features: capacity, track_ids, ptc_num, direction 
   *    grid_info : pb_graph_pin
   ***********************************************************************/

  /************************************************************************
   * 3. Create the connectivity of OPINs
   *    a. Evenly assign connections to OPINs to routing tracks
   *    b. the connection pattern should be same across the fabric
   ***********************************************************************/
  int **Fc_in = NULL; /* [0..num_types-1][0..num_pins-1] */
  boolean Fc_clipped;
  Fc_clipped = FALSE;
  Fc_in = alloc_and_load_actual_fc(L_num_types, types, chan_width,
                                   FALSE, UNI_DIRECTIONAL, &Fc_clipped, ignore_Fc_0);
  if (Fc_clipped) {
    *Warnings |= RR_GRAPH_WARN_FC_CLIPPED;
  }

  /************************************************************************
   * 4. Create the connectivity of IPINs 
   *    a. Evenly assign connections from routing tracks to IPINs
   *    b. the connection pattern should be same across the fabric
   ***********************************************************************/
  int **Fc_out = NULL; /* [0..num_types-1][0..num_pins-1] */
  Fc_clipped = FALSE;
  Fc_out = alloc_and_load_actual_fc(L_num_types, types, chan_width,
                                   TRUE, UNI_DIRECTIONAL, &Fc_clipped, ignore_Fc_0);

  /************************************************************************
   * 6. Allocate rr_graph, fill the node information
   *    For each node, fill
   *    a. basic information: coordinator(xlow, xhigh, ylow, yhigh), ptc_num
   *    b. edges (both incoming and outcoming)
   *    c. handle direct-connections
   ***********************************************************************/
  /* Alloc node lookups, count nodes, alloc rr nodes */
  /*
  rr_graph.num_rr_nodes = 0;
  rr_graph.rr_node_indices = alloc_and_load_rr_node_indices(nodes_per_chan, L_nx, L_ny,
                                                            &(rr_graph.num_rr_nodes), seg_details);
  rr_graph.rr_node = (t_rr_node *) my_malloc(sizeof(t_rr_node) * rr_graph.num_rr_nodes);
  memset(rr_node, 0, sizeof(t_rr_node) * rr_graph.num_rr_nodes);
  boolean* L_rr_edge_done = (boolean *) my_malloc(sizeof(boolean) * rr_graph.num_rr_nodes);
  memset(L_rr_edge_done, 0, sizeof(boolean) * rr_graph.num_rr_nodes);
  */

  /* handle direct-connections */
  t_clb_to_clb_directs* clb_to_clb_directs = NULL;
  if (num_directs > 0) {
    clb_to_clb_directs = alloc_and_load_clb_to_clb_directs(directs, num_directs);
  }

  /************************************************************************
   * 8. Allocate external data structures
   *    a. cost_index
   *    b. RC tree
   ***********************************************************************/
  rr_graph_externals(timing_inf, segment_inf, num_seg_types, chan_width,
                     wire_to_ipin_switch, base_cost_type);

  return rr_graph;
}


/************************************************************************
 * End of file : rr_graph_tileable_builder.c 
 ***********************************************************************/