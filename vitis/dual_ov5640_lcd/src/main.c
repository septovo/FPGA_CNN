/*
 * Single OV5640 Direct Display V1
 * Zynq-7020 / Vitis 2020.2
 *
 * Purpose:
 *   - Use CAM0 only.
 *   - Disable CAM1 completely in software.
 *   - Remove all affine registration, seam search, alpha blending and fusion.
 *   - Let one OV5640 output the FULL LCD resolution directly.
 *   - CAM0 VDMA S2MM and display VDMA MM2S share the same triple-buffer ring.
 *   - Use the existing hardware Genlock connection:
 *         CAM0 S2MM frame_ptr_out -> Display MM2S frame_ptr_in
 *     together with the existing ReadCfg.FrameDelay = 1 in vdma_api.c.
 *
 * Result:
 *   - No image fusion.
 *   - No double-image/ghosting caused by two-camera blending.
 *   - No ARM-side per-frame image processing or memcpy.
 *   - Lower latency and much lower CPU load.
 *
 * IMPORTANT:
 *   1. This version uses the physical CAM0 connector / CAM0_CH0.
 *   2. Do NOT call XAxiVdma_StartParking() for the display VDMA. Parking would
 *      defeat the circular Genlock operation used by this direct path.
 *   3. The uploaded hardware has:
 *        axi_vdma_0 : CAM0 S2MM, Genlock master
 *        axi_vdma_2 : Display MM2S, Genlock slave
 *      so the two VDMAs can safely share the same frame-buffer addresses.
 */

#include <stdio.h>
#include <string.h>

#include "sleep.h"
#include "xil_types.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "xgpio.h"
#include "xaxivdma.h"

#include "display_ctrl/display_ctrl.h"
#include "vdma_api/vdma_api.h"
#include "clk_wiz/clk_wiz.h"
#include "emio_sccb_cfg/emio_sccb_cfg.h"
#include "ov5640/ov5640_init.h"


/* ------------------------------------------------------------------------- */
/* Hardware IDs                                                              */
/* ------------------------------------------------------------------------- */
#define CAM0_VDMA_ID       XPAR_AXIVDMA_0_DEVICE_ID
#define DISP_VDMA_ID       XPAR_AXIVDMA_2_DEVICE_ID
#define DISP_VTC_ID        XPAR_VTC_0_DEVICE_ID
#define CLK_WIZ_ID         XPAR_CLK_WIZ_0_DEVICE_ID
#define AXI_GPIO_0_ID      XPAR_AXI_GPIO_0_DEVICE_ID
#define AXI_GPIO_0_CHANEL  1

#define BYTES_PIXEL        3U
#define VDMA_FRAME_STORES  3U


/* ------------------------------------------------------------------------- */
/* DDR layout                                                                */
/* ------------------------------------------------------------------------- */
/*
 * One shared RGB888 triple-buffer ring.
 *
 * Maximum mode in this project:
 *   1280 x 800 x 3 x 3 = 9,216,000 bytes
 *
 * The next historical buffer region starts at +0x02000000, so there is ample
 * space between +0x01000000 and +0x02000000.
 */
#define CAM0_FRAME_BUFFER_ADDR \
    ((u32)(XPAR_PS7_DDR_0_S_AXI_BASEADDR + 0x01000000U))


/* ------------------------------------------------------------------------- */
/* Camera orientation                                                        */
/* ------------------------------------------------------------------------- */
/*
 * Your previous image showed a horizontal mirror problem.
 *
 * 1 = set OV5640 horizontal mirror bits after initialization.
 *     With the current ov5640_init.c this changes 0x3821 from 0x01 to 0x07.
 *
 * 0 = clear OV5640 horizontal mirror bits.
 *
 * If the new single-camera image is horizontally reversed in the wrong
 * direction, change this macro from 1 to 0 and rebuild.
 */
#define SENSOR_H_MIRROR  1


/* ------------------------------------------------------------------------- */
/* Driver instances                                                          */
/* ------------------------------------------------------------------------- */
static XAxiVdma    cam0_vdma;
static XAxiVdma    disp_vdma;
static DisplayCtrl dispCtrl;
static XGpio       axi_gpio_inst;
static VideoMode   vd_mode;
static unsigned int lcd_id;


/* ------------------------------------------------------------------------- */
/* SCCB register access                                                      */
/* ------------------------------------------------------------------------- */
/*
 * These functions already exist globally in ov5640_init.c, but the uploaded
 * ov5640_init.h does not declare them.
 */
extern void sccb_write_reg16(u8 cam_ch, u16 addr, u8 data);
extern u8   sccb_read_reg16(u8 cam_ch, u16 addr);


/* ------------------------------------------------------------------------- */
/* OV5640 horizontal orientation                                             */
/* ------------------------------------------------------------------------- */
static void configure_cam0_orientation(void)
{
    u8 reg3821;

    reg3821 = sccb_read_reg16(CAM0_CH0, 0x3821U);

#if SENSOR_H_MIRROR
    reg3821 = (u8)(reg3821 | 0x06U);
#else
    reg3821 = (u8)(reg3821 & (u8)~0x06U);
#endif

    sccb_write_reg16(CAM0_CH0, 0x3821U, reg3821);
    usleep(1000);

    xil_printf("CAM0 0x3821 = 0x%02x, H-mirror setting = %d\r\n",
               reg3821,
               SENSOR_H_MIRROR);
}


/* ------------------------------------------------------------------------- */
/* LCD mode                                                                  */
/* ------------------------------------------------------------------------- */
static void select_video_mode(u16 *sensor_w,
                              u16 *sensor_h,
                              u16 *total_h,
                              u16 *total_v)
{
    /*
     * In the dual-camera version each sensor was deliberately configured to
     * half of the LCD width.
     *
     * In this single-camera version CAM0 outputs the FULL LCD resolution.
     */
    switch (lcd_id) {

        case 0x4342:
            *sensor_w = 480;
            *sensor_h = 272;
            *total_h  = 1800;
            *total_v  = 1000;
            vd_mode   = VMODE_480x272;
            break;

        case 0x4384:
        case 0x7084:
            *sensor_w = 800;
            *sensor_h = 480;
            *total_h  = 1800;
            *total_v  = 1000;
            vd_mode   = VMODE_800x480;
            break;

        case 0x7016:
            *sensor_w = 1024;
            *sensor_h = 600;
            *total_h  = 2200;
            *total_v  = 1000;
            vd_mode   = VMODE_1024x600;
            break;

        case 0x1018:
            *sensor_w = 1280;
            *sensor_h = 800;
            *total_h  = 2570;
            *total_v  = 980;
            vd_mode   = VMODE_1280x800;
            break;

        default:
            *sensor_w = 800;
            *sensor_h = 480;
            *total_h  = 1800;
            *total_v  = 1000;
            vd_mode   = VMODE_800x480;
            break;
    }
}


/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */
int main(void)
{
    u8  cam_status;

    u16 sensor_w;
    u16 sensor_h;
    u16 total_h_pixel;
    u16 total_v_pixel;

    u32 frame_bytes;
    u32 all_buffers_bytes;

    /*
     * 1. Read LCD ID.
     */
    if (XGpio_Initialize(&axi_gpio_inst,
                         AXI_GPIO_0_ID) != XST_SUCCESS) {

        xil_printf("GPIO initialize failed\r\n");
        return -1;
    }

    XGpio_SetDataDirection(&axi_gpio_inst,
                           AXI_GPIO_0_CHANEL,
                           0x07);

    lcd_id = lcd_id_read(&axi_gpio_inst,
                         AXI_GPIO_0_CHANEL);

    XGpio_SetDataDirection(&axi_gpio_inst,
                           AXI_GPIO_0_CHANEL,
                           0x00);

    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf(" Single OV5640 Direct Display V1\r\n");
    xil_printf(" CAM0 only - NO image fusion\r\n");
    xil_printf("========================================\r\n");
    xil_printf("LCD ID: 0x%x\r\n", lcd_id);


    /*
     * 2. Select full-resolution single-camera mode.
     */
    select_video_mode(&sensor_w,
                      &sensor_h,
                      &total_h_pixel,
                      &total_v_pixel);

    xil_printf("CAM0 sensor output: %dx%d RGB888\r\n",
               sensor_w,
               sensor_h);

    xil_printf("LCD output:         %dx%d\r\n",
               vd_mode.width,
               vd_mode.height);

    if ((u32)sensor_w != (u32)vd_mode.width ||
        (u32)sensor_h != (u32)vd_mode.height) {

        xil_printf("ERROR: camera/display dimensions do not match\r\n");
        return -1;
    }


    /*
     * 3. Initialize ONLY CAM0.
     *    CAM1 is intentionally not initialized and CAM1 VDMA is not started.
     */
    emio_init();

    cam_status =
        ov5640_init(CAM0_CH0,
                    sensor_w,
                    sensor_h,
                    total_h_pixel,
                    total_v_pixel);

    if (cam_status != 0U) {
        xil_printf("CAM0 OV5640 detect/init failed, status=%d\r\n",
                   cam_status);
        return -1;
    }

    xil_printf("CAM0 OV5640 initialized successfully\r\n");

    /*
     * Correct horizontal orientation at the sensor. This keeps the image on
     * the direct zero-copy hardware path.
     */
    configure_cam0_orientation();


    /*
     * 4. Prepare the shared triple-buffer ring.
     */
    frame_bytes =
        (u32)sensor_w *
        (u32)sensor_h *
        BYTES_PIXEL;

    all_buffers_bytes =
        frame_bytes *
        VDMA_FRAME_STORES;

    memset((void *)CAM0_FRAME_BUFFER_ADDR,
           0,
           all_buffers_bytes);

    Xil_DCacheFlushRange((INTPTR)CAM0_FRAME_BUFFER_ADDR,
                         all_buffers_bytes);

    xil_printf("Frame size: %u bytes\r\n",
               (unsigned int)frame_bytes);

    xil_printf("Shared triple buffer: 0x%08x - 0x%08x\r\n",
               (unsigned int)CAM0_FRAME_BUFFER_ADDR,
               (unsigned int)(CAM0_FRAME_BUFFER_ADDR +
                              all_buffers_bytes - 1U));


    /*
     * 5. Configure LCD clock and VTC.
     */
    clk_wiz_cfg(CLK_WIZ_ID,
                vd_mode.freq);

    if (DisplayInitialize(&dispCtrl,
                          DISP_VTC_ID) != XST_SUCCESS) {

        xil_printf("DisplayInitialize failed\r\n");
        return -1;
    }

    DisplaySetMode(&dispCtrl,
                   &vd_mode);


    /*
     * 6. Start CAM0 S2MM at FULL LCD width.
     */
    if (run_vdma_frame_buffer(&cam0_vdma,
                              CAM0_VDMA_ID,
                              (int)sensor_w,
                              (int)sensor_h,
                              CAM0_FRAME_BUFFER_ADDR,
                              0,
                              0,
                              ONLY_WRITE) != XST_SUCCESS) {

        xil_printf("CAM0 VDMA S2MM start failed\r\n");
        return -1;
    }

    /*
     * Allow the camera to produce several complete frames.
     */
    usleep(200000);


    /*
     * 7. Start display MM2S on THE SAME frame-buffer ring.
     *
     * The uploaded PL connects:
     *
     *   CAM0 VDMA s2mm_frame_ptr_out
     *             |
     *             +----> Display VDMA mm2s_frame_ptr_in
     *
     * and axi_vdma_2 is configured as an MM2S Genlock slave.
     *
     * The uploaded vdma_api.c already uses:
     *   ReadCfg.EnableSync = 1;
     *   ReadCfg.FrameDelay = 1;
     *
     * Therefore the display follows CAM0 and reads one completed frame behind
     * the frame currently being written.
     *
     * DO NOT use XAxiVdma_StartParking() here.
     */
    if (run_vdma_frame_buffer(&disp_vdma,
                              DISP_VDMA_ID,
                              (int)vd_mode.width,
                              (int)vd_mode.height,
                              CAM0_FRAME_BUFFER_ADDR,
                              0,
                              0,
                              ONLY_READ) != XST_SUCCESS) {

        xil_printf("Display VDMA MM2S start failed\r\n");
        return -1;
    }


    /*
     * 8. Start LCD timing.
     */
    if (DisplayStart(&dispCtrl) != XST_SUCCESS) {
        xil_printf("DisplayStart failed\r\n");
        return -1;
    }


    xil_printf("\r\n");
    xil_printf("Single-camera zero-copy display is running.\r\n");
    xil_printf("CAM1: disabled in software\r\n");
    xil_printf("Fusion: disabled\r\n");
    xil_printf("Affine warp: disabled\r\n");
    xil_printf("Alpha blend: disabled\r\n");
    xil_printf("Dynamic seam: disabled\r\n");
    xil_printf("CPU per-frame processing: none\r\n");
    xil_printf("SENSOR_H_MIRROR = %d\r\n",
               SENSOR_H_MIRROR);
    xil_printf("\r\n");


    /*
     * All image movement is now performed in hardware:
     *
     * OV5640 CAM0
     *      -> Video In to AXI4-Stream
     *      -> CAM0 VDMA S2MM
     *      -> DDR triple buffer
     *      -> Display VDMA MM2S (Genlock slave, FrameDelay=1)
     *      -> LCD
     *
     * The ARM can remain idle.
     */
    while (1) {
        usleep(1000000);
    }

    return 0;
}
