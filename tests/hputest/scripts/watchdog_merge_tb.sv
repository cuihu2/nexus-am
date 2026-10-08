`timescale 1ns/1ps
module watchdog_merge_tb;
  localparam integer LIMIT = 500000;
  reg clock = 0, reset = 1, c0_valid = 0, c1_valid = 0, ready = 0;
  reg [24:0] c0_bits = 25'h12345, c1_bits = 25'h54321;
  reg [63:0] base = 64'h12345678, length = 64'h41;
  wire cpu_ready, cmd_valid, fault;
  wire [25:0] word;
  wire [31:0] offset;
  wire [15:0] lines;
  wire [7:0] code;

  HpuCmdMerge dut(
    .clock(clock), .reset(reset), .io_hpu0Cmd_0_ready(cpu_ready),
    .io_hpu0Cmd_0_valid(c0_valid), .io_hpu0Cmd_0_bits(c0_bits),
    .io_hpu1Direct_0_valid(c1_valid), .io_hpu1Direct_0_bits(c1_bits),
    .io_hpu1Base_0(base), .io_hpu1Len_0(length), .io_cmd_ready(ready),
    .io_cmd_valid(cmd_valid), .io_cmd_bits_word(word),
    .io_cmd_bits_lineOffset(offset), .io_cmd_bits_lenLines(lines),
    .io_cmd_timeout_fault_valid(fault), .io_cmd_timeout_fault_code(code));

  task tick;
    #1 clock = 1;
    #1 clock = 0;
  endtask

  task restart;
    reset = 1; c0_valid = 0; c1_valid = 0; ready = 0;
    tick(); reset = 0; tick();
    if (fault || cmd_valid || dut.cmd_wait_count != 0)
      $fatal(1, "reset did not recover command entry");
  endtask

  task wait_for_timeout(input integer kind);
    c0_valid = kind == 0; c1_valid = kind == 1; ready = 0;
    for (integer count = 1; count < LIMIT; count = count + 1) begin
      tick();
      if (fault || !cmd_valid) $fatal(1, "early timeout kind=%0d count=%0d", kind, count);
      if (kind == 0 && (word != {1'b0,c0_bits} || offset != 0 || lines != 0))
        $fatal(1, "custom0 payload changed during stall");
      if (kind == 1 && (word != {1'b1,c1_bits} || offset != base[31:0] || lines != length[15:0]))
        $fatal(1, "custom1 payload changed during stall");
    end
    if (!cpu_ready) $fatal(1, "timeout cycle did not release Fence");
    tick();
    if (!fault || code != 8'h01 || cmd_valid || !cpu_ready)
      $fatal(1, "missing fail-stop kind=%0d", kind);
    ready = 1;
    for (integer step = 0; step < 20; step = step + 1) begin
      c0_valid = step % 3 == 0; c1_valid = step % 3 == 1;
      tick();
      if (!fault || cmd_valid || !cpu_ready) $fatal(1, "fail-stop forwarded/rearmed command");
    end
    $display("PASS custom%0d: exact %0d-cycle timeout, payload and sticky fail-stop", kind, LIMIT);
  endtask

  initial begin
    restart();
    repeat (100) tick();
    if (fault || dut.cmd_wait_count != 0) $fatal(1, "idle incorrectly accumulated time");
    c0_valid = 1;
    repeat (11) tick();
    c0_valid = 0; tick();
    if (dut.cmd_wait_count != 0) $fatal(1, "input withdrawal did not clear count");
    c0_valid = 1; repeat (11) tick();
    ready = 1; tick();
    if (fault || dut.cmd_wait_count != 0 || !cmd_valid)
      $fatal(1, "handshake did not clear count");
    ready = 0;
    repeat (LIMIT-1) tick();
    ready = 1; tick();
    if (fault || dut.cmd_wait_count != 0)
      $fatal(1, "normal handshake at deadline lost to timeout");
    $display("PASS idle/input-withdrawal/handshake reset and deadline handshake priority");
    restart(); wait_for_timeout(0);
    restart(); wait_for_timeout(1);
    restart(); c1_valid = 1; ready = 1; tick();
    if (fault || !cmd_valid || !cpu_ready || word != {1'b1,c1_bits})
      $fatal(1, "reset did not restore normal forwarding");
    $display("PASS reset recovery; RTL watchdog regression complete");
    $finish;
  end
endmodule
