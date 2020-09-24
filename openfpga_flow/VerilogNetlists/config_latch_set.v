//-----------------------------------------------------
// Design Name : config_latch
// File Name   : config_latch.v
// Function    : A Configurable Latch where data storage
//               can be updated when wl is enabled
//               Set is active high
// Coder       : Xifan TANG
//-----------------------------------------------------
module config_latch_set (
  input set, // Reset input
  input wl, // Data Enable
  input bl, // Data Input
  output Q, // Q output
  output Qb // Q output
);
//------------Internal Variables--------
reg q_reg;

//-------------Code Starts Here---------
always @ (set or bl or wl) begin
  if (set) begin
    q_reg <= 1'b1;
  end else if (1'b1 == wl) begin
    q_reg <= bl;
  end
end

`ifndef ENABLE_FORMAL_VERIFICATION
// Wire q_reg to Q
assign Q = q_reg;
assign Qb = ~q_reg;
`else
assign Q = 1'bZ;
assign Qb = !Q;
`endif

endmodule
