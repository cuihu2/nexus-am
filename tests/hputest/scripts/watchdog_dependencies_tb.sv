`timescale 1ns/1ps
module watchdog_dependencies_tb;
  localparam logic [31:0] LOAD_P0 = `TEST_DLOAD_P0, LOAD_MOD = `TEST_DLOAD_MOD;
  reg clk = 0, rst_n = 0, valid = 0, commit = 0, table_done = 0;
  reg [25:0] command = 0;
  reg [31:0] offset = 0;
  reg [15:0] length = 1;
  wire ready, cfg_ready, cfg_done, modld_fire, cfg_rd_valid, dec_err;
  wire [7:0] mod_id;
  wire extmem_valid, exec_valid, cfg_valid, sync_valid, fault;
  wire table_start;
  integer cfg_accepted = 0;

  hpu_controller ctrl(
    .clk(clk), .rst_n(rst_n), .queue_pop_valid(valid), .queue_pop_ready(ready),
    .queue_pop_cmd(command), .queue_pop_mem_line_offset(offset), .queue_pop_mem_len_lines(length),
    .extmem_issue_ready(1'b1), .exec_issue_ready(1'b1), .cfg_issue_ready(cfg_ready), .sync_issue_ready(1'b1),
    .backend_busy_any(1'b0), .backend_busy_long(1'b0), .extmem_done_pulse(1'b0),
    .exec_done_valid(1'b0), .exec_done_obj_id(3'd0), .cfg_done_valid(cfg_done),
    .hpu_mem_cfg_valid(commit), .hpu_mem_base(40'h87000000), .hpu_mem_size_lines(33'd4),
    .ext_v_set_valid(1'b0), .ext_v_set_obj_id(3'd0), .ext_v_clr_valid(1'b0), .ext_v_clr_obj_id(3'd0),
    .ext_busy_clr_valid(1'b0), .ext_busy_clr_obj_id(3'd0), .dec_err(dec_err),
    .extmem_cmd_valid(extmem_valid), .exec_cmd_valid(exec_valid), .cfg_cmd_valid(cfg_valid),
    .sync_cmd_valid(sync_valid), .extmem_fault_valid(fault),
    .pmodld_fire(modld_fire), .pmodld_mod_id(mod_id), .mod_table_load_start(table_start));
  hpu_cfg_state_regs cfg(
    .clk(clk), .rst_n(rst_n), .mod_table_load_start(table_start), .mod_table_load_done(table_done),
    .pmodld_fire(modld_fire), .pmodld_mod_id(mod_id), .cfg_rd_valid(cfg_rd_valid),
    .cfg_rd_ready(1'b1), .cfg_rd_rsp_valid(1'b0), .cfg_rd_rsp_data(2048'd0),
    .cfg_ready(cfg_ready), .cfg_done(cfg_done));

  always @(posedge clk) if (rst_n && cfg_valid) cfg_accepted <= cfg_accepted + 1;
  task tick;
    #1 clk = 1;
    #1 clk = 0;
  endtask
  task restart;
    rst_n = 0; valid = 0; commit = 0; table_done = 0;
    tick(); rst_n = 1; commit = 1; tick(); commit = 0; tick();
    if (!ctrl.hpu_mem_window_valid_r) $fatal(1, "window was not committed");
  endtask
  task first(input [31:0] instruction);
    if (!ready) $fatal(1, "decoder not ready after reset");
    command = {1'b0, instruction[31:7]}; valid = 1; tick();
    if (dec_err || !ctrl.dec_uop_valid) $fatal(1, "producer instruction did not decode legally");
  endtask

  initial begin
    restart(); first(`TEST_PADD);
    // 后排DLOAD持续有效，不能越过等待源对象的PADD；后端ready全部为1。
    command = {1'b1, LOAD_P0[31:7]}; offset = 0;
    repeat (32) begin
      tick();
      if (ready || ctrl.block_reason_local != 6 || dec_err || fault ||
          extmem_valid || exec_valid || cfg_valid || sync_valid)
        $fatal(1, "missing-source PADD did not block the later DLOAD");
    end
    $display("PASS PADD-before-DLOAD: source dependency blocks FIFO head, no execution/DMA/range fault");

    restart(); first(`TEST_PMODLD);
    command = {1'b1, LOAD_MOD[31:7]}; offset = 2;
    repeat (32) begin
      tick();
      if (ready || cfg_ready || cfg_rd_valid || dec_err || fault ||
          extmem_valid || exec_valid || cfg_valid || sync_valid || !ctrl.dec_is_cfg)
        $fatal(1, "missing-table PMODLD did not block the later table DLOAD");
    end
    $display("PASS PMODLD-before-DLOAD: table_ready=0 blocks FIFO head, no uninitialized SRAM read");
    // 正向对照：独立提供模表完成事件后，原来等待的PMODLD可以发射。
    table_done = 1; tick(); table_done = 0;
    if (!cfg_ready || !cfg_valid || !modld_fire)
      $fatal(1, "completed table load did not release PMODLD");
    tick();
    if (cfg_accepted == 0) $fatal(1, "PMODLD was not accepted after dependency became ready");
    $display("PASS table dependency release; controller/config regression complete");
    $finish;
  end
endmodule
