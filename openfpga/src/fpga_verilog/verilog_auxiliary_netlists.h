#ifndef VERILOG_AUXILIARY_NETLISTS_H
#define VERILOG_AUXILIARY_NETLISTS_H

/********************************************************************
 * Include header files that are required by function declaration
 *******************************************************************/
#include <string>
#include "circuit_library.h"
#include "verilog_options.h"

/********************************************************************
 * Function declaration
 *******************************************************************/

/* begin namespace openfpga */
namespace openfpga {

void print_include_netlists(const std::string& src_dir,
                            const std::string& circuit_name,
                            const std::string& reference_benchmark_file,
                            const CircuitLibrary& circuit_lib);

void print_verilog_preprocessing_flags_netlist(const std::string& src_dir,
                                               const FabricVerilogOption& fpga_verilog_opts);

void print_verilog_simulation_preprocessing_flags(const std::string& src_dir,
                                                  const FabricVerilogOption& fpga_verilog_opts);

} /* end namespace openfpga */

#endif 
