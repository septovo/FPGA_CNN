#include <string.h>

#include "frame_pipeline.h"
#include "xil_cache.h"
#include "xil_exception.h"
#include "xparameters.h"
#include "xstatus.h"

#define PIXEL_BYTES 3U
#define IRQ_MASK (XAXIVDMA_IXR_FRMCNT_MASK | XAXIVDMA_IXR_ERROR_MASK)
#define RESET_WAIT 100000U

static void frame_irq_handler(void *reference)
{
    FramePipeline *pipeline = (FramePipeline *)reference;
    u32 pending = XAxiVdma_IntrGetPending(&pipeline->dma, XAXIVDMA_WRITE);

    XAxiVdma_IntrClear(&pipeline->dma, pending, XAXIVDMA_WRITE);
    if ((pending & XAXIVDMA_IXR_ERROR_MASK) != 0U) {
        pipeline->last_error = (u32)XAxiVdma_GetDmaChannelErrors(
            &pipeline->dma, XAXIVDMA_WRITE);
        pipeline->irq_errors++;
    }
    if ((pending & XAXIVDMA_IXR_FRMCNT_MASK) != 0U) {
        pipeline->last_write_index = XAxiVdma_CurrFrameStore(
            &pipeline->dma, XAXIVDMA_WRITE);
        pipeline->irq_frames++;
    }
}

static int start_write_channel(FramePipeline *pipeline)
{
    XAxiVdma_DmaSetup setup;
    XAxiVdma_FrameCounter frame_counter;
    UINTPTR addr[FRAME_PIPELINE_STORES];
    u32 i;
    int status;

    memset(&setup, 0, sizeof(setup));
    setup.VertSizeInput = pipeline->height;
    setup.HoriSizeInput = (u32)pipeline->width * PIXEL_BYTES;
    setup.Stride = setup.HoriSizeInput;
    setup.FrameDelay = 0;
    setup.EnableCircularBuf = 1;
    setup.EnableSync = 1; /* VDMA1 S2MM follows CAM0 frame_ptr_out. */
    setup.PointNum = 0;
    setup.EnableFrameCounter = 0; /* Continuous video. */
    setup.FixedFrameStoreAddr = 0;

    status = XAxiVdma_DmaConfig(&pipeline->dma, XAXIVDMA_WRITE, &setup);
    if (status != XST_SUCCESS) return status;

    for (i = 0; i < FRAME_PIPELINE_STORES; ++i)
        addr[i] = (UINTPTR)pipeline->frame_addr[i];
    status = XAxiVdma_DmaSetBufferAddr(&pipeline->dma, XAXIVDMA_WRITE, addr);
    if (status != XST_SUCCESS) return status;

    memset(&frame_counter, 0, sizeof(frame_counter));
    frame_counter.ReadFrameCount = 1;
    frame_counter.WriteFrameCount = 1;
    frame_counter.ReadDelayTimerCount = 1;
    frame_counter.WriteDelayTimerCount = 1;
    status = XAxiVdma_SetFrameCounter(&pipeline->dma, &frame_counter);
    if (status != XST_SUCCESS) return status;

    XAxiVdma_IntrClear(&pipeline->dma, IRQ_MASK, XAXIVDMA_WRITE);
    XAxiVdma_IntrEnable(&pipeline->dma, IRQ_MASK, XAXIVDMA_WRITE);
    return XAxiVdma_DmaStart(&pipeline->dma, XAXIVDMA_WRITE);
}

int frame_pipeline_start(FramePipeline *pipeline, u16 width, u16 height,
                         u32 gray_base, u16 dma_device_id, u16 irq_id)
{
    XAxiVdma_Config *dma_config;
    XScuGic_Config *gic_config;
    u32 frame_bytes;
    u32 i;
    int status;

    if (pipeline == NULL || width == 0U || height == 0U ||
        width > 1280U || height > 800U ||
        (gray_base & 63U) != 0U) return XST_INVALID_PARAM;

    frame_bytes = (u32)width * (u32)height * PIXEL_BYTES;
    if ((frame_bytes & 63U) != 0U ||
        gray_base > XPAR_PS7_DDR_0_S_AXI_HIGHADDR -
                    FRAME_PIPELINE_STORES * frame_bytes + 1U)
        return XST_INVALID_PARAM;

    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->width = width;
    pipeline->height = height;
    pipeline->frame_bytes = frame_bytes;
    pipeline->irq_id = irq_id;
    for (i = 0; i < FRAME_PIPELINE_STORES; ++i)
        pipeline->frame_addr[i] = gray_base + i * frame_bytes;

    dma_config = XAxiVdma_LookupConfig(dma_device_id);
    if (dma_config == NULL || dma_config->S2MmStreamWidth != 24 ||
        dma_config->MaxFrameStoreNum < FRAME_PIPELINE_STORES)
        return XST_FAILURE;
    memset((void *)(UINTPTR)gray_base, 0, FRAME_PIPELINE_STORES * frame_bytes);
    Xil_DCacheFlushRange((INTPTR)gray_base, FRAME_PIPELINE_STORES * frame_bytes);
    status = XAxiVdma_CfgInitialize(&pipeline->dma, dma_config,
                                    dma_config->BaseAddress);
    if (status != XST_SUCCESS) return status;

    gic_config = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    if (gic_config == NULL) return XST_FAILURE;
    status = XScuGic_CfgInitialize(&pipeline->gic, gic_config,
                                    gic_config->CpuBaseAddress);
    if (status != XST_SUCCESS) return status;
    status = XScuGic_Connect(&pipeline->gic, irq_id,
                             (Xil_InterruptHandler)frame_irq_handler, pipeline);
    if (status != XST_SUCCESS) return status;
    XScuGic_Enable(&pipeline->gic, irq_id);
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler,
                                 &pipeline->gic);
    Xil_ExceptionEnable();

    return start_write_channel(pipeline);
}

/* Returns XST_SUCCESS for a copied ROI, XST_FAILURE when no stable frame is ready. */
int frame_pipeline_copy_center_roi(FramePipeline *pipeline, u8 *destination,
                                   u32 destination_capacity, u32 *last_sequence,
                                   FrameRoiInfo *info)
{
    u32 sequence_before, sequence_after, write_index, frame_index;
    u32 row, col, x, y, side, current_before, current_after, missed = 0U;
    u32 line_bytes;
    const u8 *src;

    if (pipeline == NULL || destination == NULL || last_sequence == NULL ||
        info == NULL) return XST_INVALID_PARAM;
    line_bytes = (u32)pipeline->width * PIXEL_BYTES;
    side = pipeline->width < pipeline->height ? pipeline->width : pipeline->height;
    if (side > FRAME_PIPELINE_ROI_SIDE) side = FRAME_PIPELINE_ROI_SIDE;
    if (destination_capacity < side * side) return XST_INVALID_PARAM;

    sequence_before = pipeline->irq_frames;
    write_index = pipeline->last_write_index;
    if (sequence_before == 0U || sequence_before == *last_sequence ||
        write_index >= FRAME_PIPELINE_STORES) return XST_FAILURE;
    sequence_after = pipeline->irq_frames;
    if (sequence_before != sequence_after) return XST_FAILURE;
    if (*last_sequence != 0U && sequence_before > *last_sequence + 1U)
        missed = sequence_before - *last_sequence - 1U;

    /* The current write slot is forbidden. The preceding slot has completed. */
    current_before = XAxiVdma_CurrFrameStore(&pipeline->dma, XAXIVDMA_WRITE);
    frame_index = (write_index + FRAME_PIPELINE_STORES - 1U) %
                  FRAME_PIPELINE_STORES;
    if (current_before >= FRAME_PIPELINE_STORES ||
        frame_index == current_before) return XST_FAILURE;

    x = ((u32)pipeline->width - side) / 2U;
    y = ((u32)pipeline->height - side) / 2U;
    for (row = 0; row < side; ++row) {
        src = (const u8 *)(UINTPTR)(pipeline->frame_addr[frame_index] +
                                    (y + row) * line_bytes + x * PIXEL_BYTES);
        Xil_DCacheInvalidateRange((INTPTR)src, side * PIXEL_BYTES);
        for (col = 0; col < side; ++col) {
            const u8 *pixel = src + col * PIXEL_BYTES;
            destination[row * side + col] = pixel[0];
            if (pixel[0] != pixel[1] || pixel[0] != pixel[2])
                pipeline->gray_triplet_errors++;
        }
    }

    current_after = XAxiVdma_CurrFrameStore(&pipeline->dma, XAXIVDMA_WRITE);
    sequence_after = pipeline->irq_frames;
    *last_sequence = sequence_before;
    pipeline->dropped_frames += missed;
    if (frame_index == current_after || current_after >= FRAME_PIPELINE_STORES ||
        sequence_after - sequence_before > 1U) {
        pipeline->dropped_frames++;
        return XST_FAILURE;
    }
    pipeline->copied_frames++;
    pipeline->observed_slots |= 1U << frame_index;
    info->sequence = sequence_before;
    info->frame_index = frame_index;
    info->x = (u16)x;
    info->y = (u16)y;
    info->width = (u16)side;
    info->height = (u16)side;
    return XST_SUCCESS;
}

int frame_pipeline_recover(FramePipeline *pipeline)
{
    u32 wait;
    int status;

    if (pipeline == NULL) return XST_INVALID_PARAM;
    XScuGic_Disable(&pipeline->gic, pipeline->irq_id);
    XAxiVdma_IntrDisable(&pipeline->dma, IRQ_MASK, XAXIVDMA_WRITE);
    XAxiVdma_DmaStop(&pipeline->dma, XAXIVDMA_WRITE);
    XAxiVdma_Reset(&pipeline->dma, XAXIVDMA_WRITE);
    for (wait = 0U; wait < RESET_WAIT; ++wait) {
        if (!XAxiVdma_ResetNotDone(&pipeline->dma, XAXIVDMA_WRITE)) break;
    }
    if (wait == RESET_WAIT) return XST_FAILURE;
    pipeline->irq_frames = 0U;
    pipeline->last_write_index = 0U;
    status = start_write_channel(pipeline);
    if (status == XST_SUCCESS) {
        pipeline->recoveries++;
        XScuGic_Enable(&pipeline->gic, pipeline->irq_id);
    }
    return status;
}
