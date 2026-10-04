`timescale 1ns/1ps
module tb_digit_osd_axis;
    localparam W=48, H=16, N=5*W*H;
    reg clk=0;
    always #5 clk=~clk;
    reg rstn=0;
    reg [23:0] s_data=0;
    reg s_valid=0, s_user=0, s_last=0;
    wire s_ready;
    wire [23:0] m_data;
    wire m_valid,m_user,m_last;
    reg m_ready=0;
    reg [5:0] awaddr=0,araddr=0;
    reg awvalid=0,wvalid=0,arvalid=0;
    wire awready,wready,arready;
    reg [31:0] wdata=0;
    reg [3:0] wstrb=4'hf;
    wire [1:0] bresp,rresp;
    wire bvalid,rvalid;
    reg bready=1,rready=1;
    wire [31:0] rdata;
    integer sent=0, received=0, failures=0, cycle=0;
    integer f,x,y,px,py,pf;
    reg [23:0] expected_rgb;
    reg skip_rgb;
    reg [31:0] lfsr=32'h12345678;

    digit_osd_axis dut (
        .aclk(clk),.aresetn(rstn),
        .s_axis_tdata(s_data),.s_axis_tvalid(s_valid),.s_axis_tready(s_ready),
        .s_axis_tuser(s_user),.s_axis_tlast(s_last),
        .m_axis_tdata(m_data),.m_axis_tvalid(m_valid),.m_axis_tready(m_ready),
        .m_axis_tuser(m_user),.m_axis_tlast(m_last),
        .s_axi_awaddr(awaddr),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready)
    );
    function [23:0] base_rgb(input integer n);
        base_rgb=24'h224466 ^ n;
    endfunction
    always @(posedge clk) begin
        if (!rstn) begin
            lfsr <= 32'h12345678;
            m_ready <= 0;
        end else begin
            lfsr <= {lfsr[30:0],lfsr[31]^lfsr[21]^lfsr[1]^lfsr[0]};
            m_ready <= lfsr[2] | lfsr[5];
            cycle <= cycle+1;
            if (cycle>100000) $fatal(1,"timeout");
            if (m_valid && m_ready) begin
                pf=received/(W*H);
                py=(received%(W*H))/W;
                px=received%W;
                expected_rgb=base_rgb(received);
                skip_rgb=0;
                if (pf==1) begin
                    if (px>=2 && px<6 && py>=2 && py<5 &&
                        (px==2 || px==5 || py==2 || py==4)) expected_rgb=24'h00ff00;
                    if (px>=2 && px<37 && py>=7 && py<14) skip_rgb=1;
                    if (px==3 && py==7) begin skip_rgb=0; expected_rgb=24'hffffff; end
                end
                if (pf==2) begin
                    if (px>=46 && py>=14 && (py==14 || px==46)) expected_rgb=24'h00ff00;
                    if (px>=46 && py>=5 && py<12) skip_rgb=1;
                end
                if (pf==3) begin
                    if (px>=8 && px<20 && py>=8 && py<15) skip_rgb=1;
                    if (px==8 && py==8) begin skip_rgb=0; expected_rgb=24'hffffff; end
                    if (px==16 && py==8) begin skip_rgb=0; expected_rgb=24'hffffff; end
                end
                if ((!skip_rgb && m_data !== expected_rgb) ||
                    m_user !== ((px==0)&&(py==0)) || m_last !== (px==W-1)) begin
                    $display("FAIL out=%0d f=%0d xy=%0d,%0d rgb=%h expected=%h user=%b last=%b",
                             received,pf,px,py,m_data,expected_rgb,m_user,m_last);
                    failures=failures+1;
                end
                received=received+1;
            end
        end
    end
    task axi_write(input [5:0] addr,input [31:0] data);
        begin
            @(negedge clk);awaddr=addr;awvalid=1;wdata=data;wvalid=1;
            do @(posedge clk); while (!(awready && wready));
            @(negedge clk);awvalid=0;wvalid=0;
            wait(bvalid);
            if (bresp!==0) $fatal(1,"AXI write response");
            @(posedge clk);
        end
    endtask
    task pixel(input integer frame,input integer row,input integer col);
        integer n;
        begin
            n=frame*W*H+row*W+col;
            @(negedge clk);
            s_data=base_rgb(n);s_user=(row==0 && col==0);
            s_last=(col==W-1);s_valid=1;
            do @(posedge clk); while (!s_ready);
            sent=sent+1;
            @(negedge clk);s_valid=0;s_user=0;s_last=0;
        end
    endtask
    task frame_start_config;
        begin
            axi_write(6'h00,2);axi_write(6'h04,2);axi_write(6'h08,4);
            axi_write(6'h0c,3);axi_write(6'h10,9);axi_write(6'h14,85);
            axi_write(6'h18,3);axi_write(6'h1c,111);axi_write(6'h20,5);
            axi_write(6'h24,1);
        end
    endtask
    task frame_next_config;
        begin
            axi_write(6'h00,46);axi_write(6'h04,14);axi_write(6'h08,10);
            axi_write(6'h0c,5);axi_write(6'h10,0);axi_write(6'h14,100);
            axi_write(6'h18,3);axi_write(6'h1c,222);axi_write(6'h20,1);
            axi_write(6'h24,1);
        end
    endtask
    initial begin
        repeat(5) @(posedge clk);
        @(negedge clk);rstn=1;
        for(y=0;y<H;y=y+1) for(x=0;x<W;x=x+1) pixel(0,y,x);
        frame_start_config();
        for(y=0;y<H;y=y+1) for(x=0;x<W;x=x+1) begin
            if (y==6 && x==0) frame_next_config();
            pixel(1,y,x);
        end
        for(y=0;y<H;y=y+1) for(x=0;x<W;x=x+1) pixel(2,y,x);
        for(y=0;y<H;y=y+1) for(x=0;x<W;x=x+1) pixel(3,y,x);
        axi_write(6'h18,0);axi_write(6'h24,1);
        for(y=0;y<H;y=y+1) for(x=0;x<W;x=x+1) pixel(4,y,x);
        wait(received==N);
        repeat(4) @(posedge clk);
        if (sent!=N || failures!=0) $fatal(1,"sent=%0d received=%0d failures=%0d",sent,received,failures);
        $display("STAGE6_OSD_PASS pixels=%0d frames=5",received);
        $finish;
    end
endmodule
