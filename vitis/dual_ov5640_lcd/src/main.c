/*
 * CAM0 direct display plus independent grayscale capture
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
 *   - Copy only a stable grayscale ROI in the ARM main loop.
 *   - Preserve the RGB display path while preparing PS digit input.
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
#include "xuartps_hw.h"
#include "xtime_l.h"

#include "display_ctrl/display_ctrl.h"
#include "vdma_api/vdma_api.h"
#include "clk_wiz/clk_wiz.h"
#include "emio_sccb_cfg/emio_sccb_cfg.h"
#include "ov5640/ov5640_init.h"
#include "frame_pipeline.h"
#include "digit_preprocess.h"
#include "digit_model.h"


/* ------------------------------------------------------------------------- */
/* Hardware IDs                                                              */
/* ------------------------------------------------------------------------- */
#define CAM0_VDMA_ID       XPAR_AXIVDMA_0_DEVICE_ID
#define DISP_VDMA_ID       XPAR_AXIVDMA_2_DEVICE_ID
#define GRAY_VDMA_ID       XPAR_AXIVDMA_1_DEVICE_ID
#define GRAY_VDMA_IRQ_ID   XPAR_FABRIC_AXI_VDMA_1_S2MM_INTROUT_INTR
#define DISP_VTC_ID        XPAR_VTC_0_DEVICE_ID
#define CLK_WIZ_ID         XPAR_CLK_WIZ_0_DEVICE_ID
#define AXI_GPIO_0_ID      XPAR_AXI_GPIO_0_DEVICE_ID
#define AXI_GPIO_0_CHANEL  1
#define GRAY_GPIO_CHANNEL  2
#define GRAY_MEAN_ENABLE   0U
#define PREPROCESS_EVERY_N_FRAMES 5U

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
#define GRAY_FRAME_BUFFER_ADDR \
    ((u32)(XPAR_PS7_DDR_0_S_AXI_BASEADDR + 0x02000000U))


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
static FramePipeline gray_pipeline;
static DigitPreprocessWorkspace digit_workspace;
static DigitPreprocessResult digit_result;
static u8 gray_roi[FRAME_PIPELINE_ROI_SIDE * FRAME_PIPELINE_ROI_SIDE]
    __attribute__((aligned(32)));
static DisplayCtrl dispCtrl;
static XGpio       axi_gpio_inst;
static VideoMode   vd_mode;
static unsigned int lcd_id;

static uint64_t preprocess_clock(void *unused)
{
    XTime ticks;
    (void)unused;
    XTime_GetTime(&ticks);
    return (uint64_t)ticks;
}

static u32 ticks_to_us(uint64_t ticks)
{
    return (u32)((ticks * 1000000U) / COUNTS_PER_SECOND);
}


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
    u32 last_gray_sequence = 0U;
    u32 seen_gray_errors = 0U;
    u32 quiet_loops = 0U;
    u32 last_reported_copies = 0U;
    u32 mean_enabled = GRAY_MEAN_ENABLE;
    FrameRoiInfo roi_info;
    DigitPreprocessStatus digit_status = DIGIT_NONE;
    u32 preprocess_count = 0U;
    u32 digit_count = 0U;
    u32 inference_count = 0U;
    u32 inference_errors = 0U;
    u32 last_inference_us = 0U;
    u32 max_inference_us = 0U;
    u32 last_digit = 0U;
    u32 last_confidence_permille = 0U;
    float digit_scores[DIGIT_MODEL_CLASSES];

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

    XGpio_SetDataDirection(&axi_gpio_inst, GRAY_GPIO_CHANNEL, 0x00U);
    XGpio_DiscreteWrite(&axi_gpio_inst, GRAY_GPIO_CHANNEL, GRAY_MEAN_ENABLE);

    lcd_id = lcd_id_read(&axi_gpio_inst,
                         AXI_GPIO_0_CHANEL);

    XGpio_SetDataDirection(&axi_gpio_inst,
                           AXI_GPIO_0_CHANEL,
                           0x00);

    xil_printf("\r\n");
    xil_printf("========================================\r\n");
    xil_printf(" CAM0 RGB display + grayscale capture\r\n");
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

    /* Independent grayscale ring at +0x02000000, driven by CAM0 pixels. */
    if (GRAY_FRAME_BUFFER_ADDR < CAM0_FRAME_BUFFER_ADDR + all_buffers_bytes ||
        GRAY_FRAME_BUFFER_ADDR > XPAR_PS7_DDR_0_S_AXI_HIGHADDR -
                                 all_buffers_bytes + 1U) {
        xil_printf("ERROR: grayscale DDR ring overlaps or exceeds DDR\r\n");
        return -1;
    }
    if (frame_pipeline_start(&gray_pipeline, sensor_w, sensor_h,
                             GRAY_FRAME_BUFFER_ADDR, GRAY_VDMA_ID,
                             GRAY_VDMA_IRQ_ID) != XST_SUCCESS) {
        xil_printf("Grayscale VDMA1/GIC start failed\r\n");
        return -1;
    }
    xil_printf("Grayscale triple buffer: 0x%08x - 0x%08x\r\n",
               GRAY_FRAME_BUFFER_ADDR,
               GRAY_FRAME_BUFFER_ADDR + all_buffers_bytes - 1U);

    /* Allow the camera to produce several complete frames. */
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
    xil_printf("Gray mean filter: %u, VDMA1 IRQ: %u\r\n",
               GRAY_MEAN_ENABLE, GRAY_VDMA_IRQ_ID);
    xil_printf("CNN model SHA256: %s\r\n", digit_model_sha256());
    xil_printf("CNN ops: CAST/CONV2D x2/MAXPOOL/FC/SOFTMAX; workspace=%u bytes\r\n",
               DIGIT_MODEL_WORKSPACE_BYTES);
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
     * The ARM main loop copies a stable grayscale ROI for later CNN input.
     */
    while (1) {
        if (XUartPs_IsReceiveData(STDOUT_BASEADDRESS)) {
            u32 key = XUartPs_ReadReg(STDOUT_BASEADDRESS,
                                      XUARTPS_FIFO_OFFSET) & 0xFFU;
            if (key == (u32)'0' || key == (u32)'1') {
                mean_enabled = key - (u32)'0';
                XGpio_DiscreteWrite(&axi_gpio_inst, GRAY_GPIO_CHANNEL,
                                    mean_enabled);
                xil_printf("Gray mean filter=%u (effective next VSYNC)\r\n",
                           mean_enabled);
            } else if (key == (u32)'p' || key == (u32)'g') {
                u32 i;
                if (digit_status != DIGIT_FOUND) {
                    xil_printf("No digit tensor available\r\n");
                } else {
                    xil_printf("tensor28=");
                    for (i = 0; i < DIGIT_MODEL_BYTES; ++i)
                        xil_printf("%02x", digit_result.tensor[i]);
                    xil_printf("\r\n");
                    if (key == (u32)'g' && inference_count != 0U) {
                        xil_printf("scores_ppm=");
                        for (i = 0U; i < DIGIT_MODEL_CLASSES; ++i)
                            xil_printf("%u%s",
                                       (u32)(digit_scores[i] * 1000000.0f + 0.5f),
                                       i == DIGIT_MODEL_CLASSES - 1U ? "" : ",");
                        xil_printf("\r\n");
                    }
                }
            } else if (key == (u32)'v') {
                u32 cls;
                if (inference_count == 0U) {
                    xil_printf("No CNN scores available\r\n");
                } else {
                    xil_printf("scores_ppm=");
                    for (cls = 0U; cls < DIGIT_MODEL_CLASSES; ++cls)
                        xil_printf("%u%s",
                                   (u32)(digit_scores[cls] * 1000000.0f + 0.5f),
                                   cls == DIGIT_MODEL_CLASSES - 1U ? "" : ",");
                    xil_printf("\r\n");
                }
            } else if (key == (u32)'s') {
                xil_printf("VDMA1 irq=%u copied=%u dropped=%u "
                           "triplet=%u slots=0x%x errors=%u recoveries=%u "
                           "preprocessed=%u digits=%u infer=%u infer_errors=%u "
                           "last_digit=%u confidence_per_mille=%u "
                           "last_infer_us=%u max_infer_us=%u\r\n",
                           gray_pipeline.irq_frames,
                           gray_pipeline.copied_frames,
                           gray_pipeline.dropped_frames,
                           gray_pipeline.gray_triplet_errors,
                           gray_pipeline.observed_slots,
                           gray_pipeline.irq_errors,
                           gray_pipeline.recoveries,
                           preprocess_count, digit_count,
                           inference_count, inference_errors, last_digit,
                           last_confidence_permille, last_inference_us,
                           max_inference_us);
            }
        }
        if (gray_pipeline.irq_errors != seen_gray_errors) {
            seen_gray_errors = gray_pipeline.irq_errors;
            xil_printf("VDMA1 error=0x%08x; restarting write channel\r\n",
                       gray_pipeline.last_error);
            if (frame_pipeline_recover(&gray_pipeline) != XST_SUCCESS) {
                xil_printf("VDMA1 recovery failed\r\n");
                return -1;
            }
            last_gray_sequence = 0U;
        }

        if (frame_pipeline_copy_center_roi(&gray_pipeline, gray_roi,
                sizeof(gray_roi), &last_gray_sequence,
                &roi_info) == XST_SUCCESS) {
            u32 sum = 0U;
            u32 i;
            quiet_loops = 0U;
            if ((gray_pipeline.copied_frames % PREPROCESS_EVERY_N_FRAMES) == 0U) {
                digit_status = digit_preprocess(
                    gray_roi, roi_info.width, roi_info.height,
                    roi_info.width, roi_info.x, roi_info.y,
                    &digit_workspace, &digit_result,
                    preprocess_clock, NULL);
                ++preprocess_count;
                if (digit_status == DIGIT_FOUND) {
                    uint64_t infer_start, infer_end;
                    unsigned int best = 0U;
                    unsigned int cls;
                    int inference_status;
                    ++digit_count;
                    infer_start = preprocess_clock(NULL);
                    inference_status = digit_model_infer(digit_result.tensor,
                                                         digit_scores);
                    infer_end = preprocess_clock(NULL);
                    last_inference_us = ticks_to_us(infer_end - infer_start);
                    if (last_inference_us > max_inference_us)
                        max_inference_us = last_inference_us;
                    if (inference_status != 0) {
                        ++inference_errors;
                        xil_printf("CNN inference failed: %d\r\n",
                                   inference_status);
                    } else {
                        for (cls = 1U; cls < DIGIT_MODEL_CLASSES; ++cls)
                            if (digit_scores[cls] > digit_scores[best])
                                best = cls;
                        last_digit = best;
                        last_confidence_permille =
                            (u32)(digit_scores[best] * 1000.0f + 0.5f);
                        ++inference_count;
                        if (inference_count <= 3U ||
                            inference_count % 12U == 0U)
                            xil_printf("CNN digit=%u confidence=%u/1000 "
                                       "infer_us=%u count=%u\r\n",
                                       last_digit, last_confidence_permille,
                                       last_inference_us, inference_count);
                    }
                }
                if (preprocess_count <= 3U || preprocess_count % 60U == 0U) {
                    xil_printf("Preprocess status=%d box=(%u,%u %ux%u) "
                               "area=%u otsu=%u count=%u times_us=%u/%u/%u/%u\r\n",
                               digit_status, digit_result.x, digit_result.y,
                               digit_result.width, digit_result.height,
                               digit_result.foreground_pixels,
                               digit_result.otsu_threshold,
                               digit_result.components,
                               ticks_to_us(digit_result.threshold_ticks),
                               ticks_to_us(digit_result.morphology_ticks),
                               ticks_to_us(digit_result.component_ticks),
                               ticks_to_us(digit_result.resize_ticks));
                }
            }
            if (gray_pipeline.copied_frames <= 6U ||
                gray_pipeline.copied_frames - last_reported_copies >= 120U) {
                for (i = 0; i < (u32)roi_info.width * roi_info.height; ++i)
                    sum += gray_roi[i];
                xil_printf("Gray seq=%u slot=%u ROI=(%u,%u %ux%u) sum=%u "
                           "copied=%u dropped=%u triplet=%u errors=%u\r\n",
                           roi_info.sequence, roi_info.frame_index,
                           roi_info.x, roi_info.y, roi_info.width,
                           roi_info.height, sum,
                           gray_pipeline.copied_frames,
                           gray_pipeline.dropped_frames,
                           gray_pipeline.gray_triplet_errors,
                           gray_pipeline.irq_errors);
                last_reported_copies = gray_pipeline.copied_frames;
            }
        } else if (++quiet_loops >= 5000U) {
            xil_printf("VDMA1 frame timeout; restarting write channel\r\n");
            if (frame_pipeline_recover(&gray_pipeline) != XST_SUCCESS)
                return -1;
            last_gray_sequence = 0U;
            quiet_loops = 0U;
        }
        usleep(1000);
    }

    return 0;
}
