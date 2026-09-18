`timescale 1ns/1ps
// CAM0 native-video preprocessing for the independent VDMA1 capture path.
// One output pixel is produced for every enabled input pixel, with no clock
// latency. The optional 3x3 mean is causal: the current pixel is the lower
// right corner of the window. The first two rows/columns pass grayscale Y.
module gray_mean_cam0 #(
    parameter integer MAX_WIDTH = 1280
)(
    input  wire        pixel_clk,
    input  wire        rst_n,
    input  wire        filter_enable,
    input  wire        pixel_ce_in,
    input  wire        frame_vsync_in,
    input  wire        pixel_active_in,
    input  wire [23:0] rgb_in,       // {R,G,B} from ov5640_capture_data
    output wire        pixel_ce_out,
    output wire        frame_vsync_out,
    output wire        pixel_active_out,
    output wire [23:0] gray_rgb_out       // {Y,Y,Y} for 24-bit VDMA1
);
    localparam integer COL_BITS = $clog2(MAX_WIDTH + 1);

    reg [7:0] row_prev [0:MAX_WIDTH-1];
    reg [7:0] row_prev2 [0:MAX_WIDTH-1];
    reg [COL_BITS-1:0] col;
    reg [15:0] row;
    reg was_active;
    reg filter_meta, filter_sync, filter_frame;
    reg [7:0] top_l1, top_l2, mid_l1, mid_l2, bot_l1, bot_l2;

    wire [7:0] red   = rgb_in[23:16];
    wire [7:0] green = rgb_in[15:8];
    wire [7:0] blue  = rgb_in[7:0];
    wire [15:0] weighted = red * 8'd77 + green * 8'd150 + blue * 8'd29 + 16'd128;
    wire [7:0] gray = weighted[15:8];
    wire [7:0] top = (col < MAX_WIDTH) ? row_prev2[col] : 8'd0;
    wire [7:0] mid = (col < MAX_WIDTH) ? row_prev[col] : 8'd0;
    wire [11:0] sum9 = top_l2 + top_l1 + top +
                       mid_l2 + mid_l1 + mid +
                       bot_l2 + bot_l1 + gray;
    wire [7:0] mean9 = sum9 / 12'd9;
    wire [7:0] output_y = (filter_frame && row >= 2 && col >= 2 && col < MAX_WIDTH)
                        ? mean9 : gray;

    assign pixel_ce_out = pixel_ce_in;
    assign frame_vsync_out = frame_vsync_in;
    assign pixel_active_out = pixel_active_in;
    assign gray_rgb_out = {output_y, output_y, output_y};

    // Synchronize the AXI GPIO bit and apply changes only at a new frame.
    always @(posedge pixel_clk or negedge rst_n) begin
        if (!rst_n) begin
            filter_meta <= 0;
            filter_sync <= 0;
            filter_frame <= 0;
        end else begin
            filter_meta <= filter_enable;
            filter_sync <= filter_meta;
            if (pixel_ce_in && frame_vsync_in)
                filter_frame <= filter_sync;
        end
    end
    always @(posedge pixel_clk or negedge rst_n) begin
        if (!rst_n) begin
            col <= 0;
            row <= 0;
            was_active <= 0;
            top_l1 <= 0; top_l2 <= 0;
            mid_l1 <= 0; mid_l2 <= 0;
            bot_l1 <= 0; bot_l2 <= 0;
        end else if (pixel_ce_in) begin
            if (frame_vsync_in) begin
                col <= 0;
                row <= 0;
                was_active <= 0;
                top_l1 <= 0; top_l2 <= 0;
                mid_l1 <= 0; mid_l2 <= 0;
                bot_l1 <= 0; bot_l2 <= 0;
            end else if (pixel_active_in) begin
                was_active <= 1;
                if (col < MAX_WIDTH) begin
                    row_prev2[col] <= row_prev[col];
                    row_prev[col] <= gray;
                    top_l2 <= top_l1; top_l1 <= top;
                    mid_l2 <= mid_l1; mid_l1 <= mid;
                    bot_l2 <= bot_l1; bot_l1 <= gray;
                    col <= col + 1'b1;
                end
            end else if (was_active) begin
                was_active <= 0;
                col <= 0;
                row <= row + 1'b1;
                top_l1 <= 0; top_l2 <= 0;
                mid_l1 <= 0; mid_l2 <= 0;
                bot_l1 <= 0; bot_l2 <= 0;
            end
        end
    end
endmodule
