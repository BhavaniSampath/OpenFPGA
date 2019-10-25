/********************************************************************
 * This file includes functions to build bitstream from routing multiplexers
 * which are based on different technology
 *******************************************************************/
#include "vtr_assert.h"

#include "mux_utils.h"
#include "fpga_x2p_types.h"

#include "build_mux_bitstream.h"

/********************************************************************
 * Find the default path id of a MUX
 * This is applied when the path id specified is DEFAULT_PATH_ID,
 * which is not correlated to the MUX implementation 
 * This function is binding the default path id to the implemented structure
 * 1. If the MUX has a constant input, the default path id will be 
 *    directed to the last input of the MUX, which is the constant input
 * 2. If the MUX does not have a constant input, the default path id 
 *    will the first input of the MUX.
 *
 * Restriction:
 *   we assume the default path is the first input of the MUX
 *   Change if this is not what you want
 *******************************************************************/
size_t find_mux_default_path_id(const CircuitLibrary& circuit_lib,
                                const CircuitModelId& mux_model,
                                const size_t& mux_size) {
  size_t default_path_id;

  if (TRUE == circuit_lib.mux_add_const_input(mux_model)) {
    default_path_id = mux_size; /* When there is a constant input, use the last path */
  } else {
    default_path_id = DEFAULT_MUX_PATH_ID; /* When there is no constant input, use the default one */
  }

  return default_path_id; 
}

/********************************************************************
 * This function generates bitstream for a CMOS routing multiplexer
 * Thanks to MuxGraph object has already describe the internal multiplexing 
 * structure, bitstream generation is simply done by routing the signal
 * to from a given input to the output
 * All the memory bits can be generated by an API of MuxGraph
 *
 * To be generic, this function only returns a vector bit values
 * without touching an bitstream-relate data structure
 *******************************************************************/
static 
std::vector<bool> build_cmos_mux_bitstream(const CircuitLibrary& circuit_lib,
                                           const CircuitModelId& mux_model,
                                           const MuxLibrary& mux_lib,
                                           const size_t& mux_size,
                                           const int& path_id) {
  /* Note that the size of implemented mux could be different than the mux size we see here,
   * due to the constant inputs 
   * We will find the input size of implemented MUX and fetch the graph-based representation in MUX library 
   */
  size_t implemented_mux_size = find_mux_implementation_num_inputs(circuit_lib, mux_model, mux_size);
  MuxId mux_graph_id = mux_lib.mux_graph(mux_model, implemented_mux_size);
  const MuxGraph mux_graph = mux_lib.mux_graph(mux_graph_id);

  size_t datapath_id = path_id;

  /* Find the path_id related to the implementation */
  if (DEFAULT_PATH_ID == path_id) {
    datapath_id = find_mux_default_path_id(circuit_lib, mux_model, implemented_mux_size);
  } else { 
    VTR_ASSERT( datapath_id < mux_size);
  }

  /* We should have only one output for this MUX! */
  VTR_ASSERT(1 == mux_graph.outputs().size());

  /* Generate the memory bits */
  return mux_graph.decode_memory_bits(MuxInputId(datapath_id), mux_graph.output_id(mux_graph.outputs()[0]));
}

/********************************************************************
 * This function generates bitstream for a routing multiplexer
 * supporting both CMOS and ReRAM multiplexer designs 
 *******************************************************************/
std::vector<bool> build_mux_bitstream(const CircuitLibrary& circuit_lib,
                                      const CircuitModelId& mux_model,
                                      const MuxLibrary& mux_lib,
                                      const size_t& mux_size,
                                      const int& path_id) {
  std::vector<bool> mux_bitstream;

  switch (circuit_lib.design_tech_type(mux_model)) {
  case SPICE_MODEL_DESIGN_CMOS:
    mux_bitstream = build_cmos_mux_bitstream(circuit_lib, mux_model, mux_lib, mux_size, path_id);
    break;
  case SPICE_MODEL_DESIGN_RRAM:
    /* TODO: ReRAM MUX needs a different bitstream generation strategy */
    break;
  default:
    vpr_printf(TIO_MESSAGE_ERROR,
               "(File:%s,[LINE%d])Invalid design technology for circuit model (%s)!\n",
               __FILE__, __LINE__, circuit_lib.model_name(mux_model).c_str());
    exit(1);
  }
  return mux_bitstream;
}
