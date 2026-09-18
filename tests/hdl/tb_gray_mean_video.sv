`timescale 1ns/1ps
module tb_gray_mean_video;
    localparam W = 5;
    localparam H = 5;
    reg clk = 0;
    always #5 clk = ~clk;
    reg rst_n = 0, enable = 0, ce = 0, vsync = 0, active = 0;
    reg [23:0] rgb = 0;
    wire out_ce, out_vsync, out_active;
    wire [23:0] out_rgb;
    integer frame, r, c, t, k, failures = 0;
    integer gray [0:H-1][0:W-1];
    integer expected, total;

    gray_mean_cam0 #(.MAX_WIDTH(W)) dut (
        .pixel_clk(clk), .rst_n(rst_n), .filter_enable(enable),
        .pixel_ce_in(ce), .frame_vsync_in(vsync), .pixel_active_in(active),
        .rgb_in(rgb), .pixel_ce_out(out_ce), .frame_vsync_out(out_vsync),
        .pixel_active_out(out_active), .gray_rgb_out(out_rgb)
    );

    task tick;
        begin
            @(posedge clk);
            #1;
        end
    endtask

    task sample_pixel(input integer red, green, blue, input integer row_i, col_i);
        begin
            repeat ((row_i*7 + col_i*3 + frame) % 4) begin
                @(negedge clk); ce = 0; active = 1; rgb = 24'hffffff;
                tick();
            end
            @(negedge clk);
            ce = 1; active = 1;
            rgb = {red[7:0], green[7:0], blue[7:0]};
            gray[row_i][col_i] = (77*red + 150*green + 29*blue + 128) >> 8;
            expected = gray[row_i][col_i];
            if (frame != 0 && row_i >= 2 && col_i >= 2) begin
                total = 0;
                for (t = row_i - 2; t <= row_i; t = t + 1)
                    for (k = col_i - 2; k <= col_i; k = k + 1)
                        total = total + gray[t][k];
                expected = total / 9;
            end
            #1;
            if (out_rgb !== {3{expected[7:0]}} || out_ce !== ce ||
                out_vsync !== vsync || out_active !== active) begin
                $display("FAIL f=%0d r=%0d c=%0d got=%h expected=%0d", frame, row_i, col_i, out_rgb, expected);
                failures = failures + 1;
            end
            tick();
            @(negedge clk);
            ce = 0;
            #1;
            if (out_ce !== 0) failures = failures + 1;
            tick();
        end
    endtask

    initial begin
        repeat (3) tick();
        @(negedge clk); rst_n = 1;
        for (frame = 0; frame < 3; frame = frame + 1) begin
            if (frame == 2) begin
                @(negedge clk); ce = 1; active = 1; rgb = 24'h123456;
                tick();
                @(negedge clk); rst_n = 0;
                tick();
                @(negedge clk); rst_n = 1; ce = 0; active = 0;
            end
            enable = (frame != 0);
            repeat (3) tick();
            ce = 1; vsync = 1; active = 0;
            tick();
            @(negedge clk); vsync = 0; ce = 0;
            for (r = 0; r < H; r = r + 1) begin
                for (c = 0; c < W; c = c + 1) begin
                    if (r == 0 && c == 0) sample_pixel(0,0,0,r,c);
                    else if (r == 0 && c == 1) sample_pixel(255,255,255,r,c);
                    else if (r == 0 && c == 2) sample_pixel(255,0,0,r,c);
                    else if (r == 0 && c == 3) sample_pixel(0,255,0,r,c);
                    else if (r == 0 && c == 4) sample_pixel(0,0,255,r,c);
                    else sample_pixel((r*37+c*11+frame*3)%256,
                                      (r*17+c*41+frame*7)%256,
                                      (r*53+c*13+frame*5)%256, r, c);
                end
                if (frame == 1 && r == 2) enable = 0;
                @(negedge clk); ce = 1; active = 0;
                tick();
                @(negedge clk); ce = 0;
                tick();
            end
        end
        if (failures) $fatal(1, "%0d checks failed", failures);
        $display("PASS: grayscale, mean, blanking, CE stalls, three frames, mid-frame mode change, mid-line reset");
        $finish;
    end
endmodule
