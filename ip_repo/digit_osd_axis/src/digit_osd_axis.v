`timescale 1ns/1ps
// RGB888 AXI4-Stream OSD. AXI-Lite and stream share aclk (FCLK_CLK1).
// One output register preserves TLAST/TUSER/data under arbitrary backpressure.
module digit_osd_axis (
    input  wire        aclk,
    input  wire        aresetn,
    input  wire [23:0] s_axis_tdata,
    input  wire        s_axis_tvalid,
    output wire        s_axis_tready,
    input  wire        s_axis_tuser,
    input  wire        s_axis_tlast,
    output reg  [23:0] m_axis_tdata,
    output reg         m_axis_tvalid,
    input  wire        m_axis_tready,
    output reg         m_axis_tuser,
    output reg         m_axis_tlast,
    input  wire [5:0]  s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output reg  [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [5:0]  s_axi_araddr,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output reg  [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready
);
    // Config: x,y,w,h [12 each], class [4], score [7], valid, enable,
    // timeout_frames [16], source_sequence [32]. 0x24 COMMIT snapshots shadow.
    reg [108:0] shadow_cfg, pending_cfg, active_cfg;
    reg pending_commit;
    reg [12:0] pending_right, pending_bottom, pending_label_y;
    reg [12:0] active_right, active_bottom, active_label_y;
    reg [12:0] p_right, p_bottom, p_label_y;
    wire [12:0] frame_right = pending_commit ? pending_right : active_right;
    wire [12:0] frame_bottom = pending_commit ? pending_bottom : active_bottom;
    wire [12:0] frame_label_y = pending_commit ? pending_label_y : active_label_y;
    reg [15:0] active_ttl;
    reg [5:0] aw_addr, ar_addr;
    reg aw_have, w_have;
    reg [31:0] w_data;
    reg [3:0] w_strb;
    reg [11:0] next_x, next_y;
    reg [11:0] p_x, p_y;
    reg [23:0] p_data;
    reg p_valid, p_user, p_last;
    reg [23:0] q_data;
    reg q_valid, q_user, q_last;
    reg [108:0] p_cfg;
    wire [11:0] pixel_x = p_x;
    wire [11:0] pixel_y = p_y;
    wire [108:0] frame_cfg = pending_commit ? pending_cfg :
        (active_ttl <= 16'd1 ? (active_cfg & ~(109'd1 << 59)) : active_cfg);
    wire [108:0] pixel_cfg = p_cfg;
    wire [11:0] box_x = pixel_cfg[11:0];
    wire [11:0] box_y = pixel_cfg[23:12];
    wire [11:0] box_w = pixel_cfg[35:24];
    wire [11:0] box_h = pixel_cfg[47:36];
    wire [3:0] digit = pixel_cfg[51:48];
    wire [6:0] score = pixel_cfg[58:52];
    wire valid = pixel_cfg[59];
    wire enabled = pixel_cfg[60];
    wire [12:0] right_edge = p_right;
    wire [12:0] bottom_edge = p_bottom;
    wire in_box = box_w != 0 && box_h != 0 &&
                  {1'b0,pixel_x} >= {1'b0,box_x} && {1'b0,pixel_x} < right_edge &&
                  {1'b0,pixel_y} >= {1'b0,box_y} && {1'b0,pixel_y} < bottom_edge;
    wire border = in_box && ((pixel_x == box_x) || ({1'b0,pixel_x} == right_edge-13'd1) ||
                             (pixel_y == box_y) || ({1'b0,pixel_y} == bottom_edge-13'd1));
    wire [12:0] label_y = p_label_y;
    wire [12:0] label_dx = {1'b0,pixel_x}-{1'b0,box_x};
    wire [12:0] label_dy = {1'b0,pixel_y}-label_y;
    wire [12:0] no_dx = {1'b0,pixel_x}-13'd8;
    wire [12:0] no_dy = {1'b0,pixel_y}-13'd8;
    reg [3:0] char_code;
    reg [2:0] char_x;
    reg char_on;
    reg [34:0] bitmap;
    integer index;

    function [31:0] merge32;
        input [31:0] old_value, new_value;
        input [3:0] strobes;
        integer k;
        begin
            merge32 = old_value;
            for (k=0; k<4; k=k+1)
                if (strobes[k]) merge32[k*8 +: 8] = new_value[k*8 +: 8];
        end
    endfunction

    // 5x7 glyphs, MSB is upper-left pixel. 10=N, 11=O, 12=%, 15=blank.
    function [34:0] glyph;
        input [3:0] code;
        begin
            case (code)
                0: glyph = 35'b01110_10001_10011_10101_11001_10001_01110;
                1: glyph = 35'b00100_01100_00100_00100_00100_00100_01110;
                2: glyph = 35'b01110_10001_00001_00010_00100_01000_11111;
                3: glyph = 35'b11110_00001_00001_01110_00001_00001_11110;
                4: glyph = 35'b00010_00110_01010_10010_11111_00010_00010;
                5: glyph = 35'b11111_10000_11110_00001_00001_10001_01110;
                6: glyph = 35'b00110_01000_10000_11110_10001_10001_01110;
                7: glyph = 35'b11111_00001_00010_00100_01000_01000_01000;
                8: glyph = 35'b01110_10001_10001_01110_10001_10001_01110;
                9: glyph = 35'b01110_10001_10001_01111_00001_00010_11100;
                10: glyph = 35'b10001_11001_10101_10011_10001_10001_10001;
                11: glyph = 35'b01110_10001_10001_10001_10001_10001_01110;
                12: glyph = 35'b11001_11010_00100_00100_01011_10011_00000;
                default: glyph = 35'd0;
            endcase
        end
    endfunction

    always @* begin
        char_code = 4'd15;
        char_x = 3'd0;
        if (enabled && valid && digit <= 9 && score <= 100 &&
            label_dy < 7 && label_dx < 35) begin
            if (label_dx < 7) begin
                char_x = label_dx[2:0]; char_code = digit;
            end else if (label_dx < 14) begin
                char_x = label_dx - 13'd7;
                char_code = score == 100 ? 4'd1 : 4'd15;
            end else if (label_dx < 21) begin
                char_x = label_dx - 13'd14;
                char_code = (score / 10) % 10;
            end else if (label_dx < 28) begin
                char_x = label_dx - 13'd21;
                char_code = score % 10;
            end else begin
                char_x = label_dx - 13'd28; char_code = 4'd12;
            end
            index = 34 - (label_dy * 5 + char_x);
        end else if (enabled && !valid && no_dy < 7 && no_dx < 12) begin
            char_x = no_dx < 7 ? no_dx[2:0] : no_dx - 13'd7;
            char_code = no_dx < 7 ? 4'd10 : 4'd11;
            index = 34 - (no_dy * 5 + char_x);
        end else index = 0;
        bitmap = glyph(char_code);
        char_on = (char_x < 5) && bitmap[index];
    end
    wire [23:0] painted = (enabled && valid && border) ? 24'h00ff00 :
                          (enabled && char_on) ? 24'hffffff : p_data;

    wire output_ready = !m_axis_tvalid || m_axis_tready;
    wire q_ready = !q_valid || output_ready;
    assign s_axis_tready = !p_valid || q_ready;
    assign s_axi_awready = !aw_have && !s_axi_bvalid;
    assign s_axi_wready = !w_have && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid;

    always @(posedge aclk) begin
        if (!aresetn) begin
            m_axis_tdata <= 0; m_axis_tvalid <= 0;
            m_axis_tuser <= 0; m_axis_tlast <= 0;
            next_x <= 0; next_y <= 0;
            p_x <= 0; p_y <= 0; p_data <= 0;
            p_valid <= 0; p_user <= 0; p_last <= 0; p_cfg <= 0;
            q_data <= 0; q_valid <= 0; q_user <= 0; q_last <= 0;
            pending_right <= 0; pending_bottom <= 0; pending_label_y <= 0;
            active_right <= 0; active_bottom <= 0; active_label_y <= 0;
            p_right <= 0; p_bottom <= 0; p_label_y <= 0;
            shadow_cfg <= 0; pending_cfg <= 0; active_cfg <= 0;
            pending_commit <= 0; active_ttl <= 0;
            aw_have <= 0; w_have <= 0; aw_addr <= 0;
            w_data <= 0; w_strb <= 0; s_axi_bvalid <= 0; s_axi_bresp <= 0;
            s_axi_rvalid <= 0; s_axi_rresp <= 0; s_axi_rdata <= 0; ar_addr <= 0;
        end else begin
            if (output_ready) begin
                m_axis_tvalid <= q_valid;
                if (q_valid) begin
                    m_axis_tdata <= q_data;
                    m_axis_tuser <= q_user;
                    m_axis_tlast <= q_last;
                end
            end
            if (q_ready) begin
                q_valid <= p_valid;
                if (p_valid) begin
                    q_data <= painted;
                    q_user <= p_user;
                    q_last <= p_last;
                end
            end
            if (s_axis_tready) begin
                p_valid <= s_axis_tvalid;
                if (s_axis_tvalid) begin
                    p_data <= s_axis_tdata;
                    p_user <= s_axis_tuser;
                    p_last <= s_axis_tlast;
                    p_x <= s_axis_tuser ? 12'd0 : next_x;
                    p_y <= s_axis_tuser ? 12'd0 : next_y;
                    p_cfg <= s_axis_tuser ? frame_cfg : active_cfg;
                    p_right <= s_axis_tuser ? frame_right : active_right;
                    p_bottom <= s_axis_tuser ? frame_bottom : active_bottom;
                    p_label_y <= s_axis_tuser ? frame_label_y : active_label_y;
                    if (s_axis_tuser) begin
                        next_x <= s_axis_tlast ? 12'd0 : 12'd1;
                        next_y <= s_axis_tlast ? 12'd1 : 12'd0;
                        active_cfg <= frame_cfg;
                        active_right <= frame_right;
                        active_bottom <= frame_bottom;
                        active_label_y <= frame_label_y;
                        if (pending_commit) begin
                            active_ttl <= pending_cfg[76:61];
                            pending_commit <= 0;
                        end else if (active_ttl != 0) active_ttl <= active_ttl-16'd1;
                    end else if (s_axis_tlast) begin
                        next_x <= 0;
                        next_y <= next_y + 12'd1;
                    end else next_x <= next_x + 12'd1;
                end
            end
            if (s_axi_awvalid && s_axi_awready) begin
                aw_addr <= s_axi_awaddr;
                aw_have <= 1;
            end
            if (s_axi_wvalid && s_axi_wready) begin
                w_data <= s_axi_wdata;
                w_strb <= s_axi_wstrb;
                w_have <= 1;
            end
            if (aw_have && w_have && !s_axi_bvalid) begin
                case (aw_addr[5:2])
                    0: shadow_cfg[11:0] <= merge32({20'd0,shadow_cfg[11:0]},w_data,w_strb);
                    1: shadow_cfg[23:12] <= merge32({20'd0,shadow_cfg[23:12]},w_data,w_strb);
                    2: shadow_cfg[35:24] <= merge32({20'd0,shadow_cfg[35:24]},w_data,w_strb);
                    3: shadow_cfg[47:36] <= merge32({20'd0,shadow_cfg[47:36]},w_data,w_strb);
                    4: shadow_cfg[51:48] <= merge32({28'd0,shadow_cfg[51:48]},w_data,w_strb);
                    5: shadow_cfg[58:52] <= merge32({25'd0,shadow_cfg[58:52]},w_data,w_strb);
                    6: shadow_cfg[60:59] <= merge32({30'd0,shadow_cfg[60:59]},w_data,w_strb);
                    7: shadow_cfg[108:77] <= merge32(shadow_cfg[108:77],w_data,w_strb);
                    8: shadow_cfg[76:61] <= merge32({16'd0,shadow_cfg[76:61]},w_data,w_strb);
                    9: if (w_strb[0] && w_data[0]) begin
                           pending_cfg <= shadow_cfg;
                           pending_right <= {1'b0,shadow_cfg[11:0]} + {1'b0,shadow_cfg[35:24]};
                           pending_bottom <= {1'b0,shadow_cfg[23:12]} + {1'b0,shadow_cfg[47:36]};
                           pending_label_y <= shadow_cfg[23:12] >= 12'd9 ?
                               {1'b0,shadow_cfg[23:12]} - 13'd9 :
                               {1'b0,shadow_cfg[23:12]} + {1'b0,shadow_cfg[47:36]} + 13'd2;
                           pending_commit <= 1;
                       end
                    default: ;
                endcase
                aw_have <= 0; w_have <= 0;
                s_axi_bresp <= 0; s_axi_bvalid <= 1;
            end else if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 0;
            if (s_axi_arvalid && s_axi_arready) begin
                ar_addr <= s_axi_araddr;
                s_axi_rresp <= 0;
                s_axi_rvalid <= 1;
                case (s_axi_araddr[5:2])
                    0: s_axi_rdata <= {20'd0,shadow_cfg[11:0]};
                    1: s_axi_rdata <= {20'd0,shadow_cfg[23:12]};
                    2: s_axi_rdata <= {20'd0,shadow_cfg[35:24]};
                    3: s_axi_rdata <= {20'd0,shadow_cfg[47:36]};
                    4: s_axi_rdata <= {28'd0,shadow_cfg[51:48]};
                    5: s_axi_rdata <= {25'd0,shadow_cfg[58:52]};
                    6: s_axi_rdata <= {30'd0,shadow_cfg[60:59]};
                    7: s_axi_rdata <= shadow_cfg[108:77];
                    8: s_axi_rdata <= {16'd0,shadow_cfg[76:61]};
                    10: s_axi_rdata <= {14'd0,pending_commit,active_cfg[59],active_ttl};
                    11: s_axi_rdata <= active_cfg[108:77];
                    default: s_axi_rdata <= 0;
                endcase
            end else if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 0;
        end
    end
endmodule
