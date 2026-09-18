#ifndef FRAME_PIPELINE_H_
#define FRAME_PIPELINE_H_

#include "xaxivdma.h"
#include "xscugic.h"
#include "xil_types.h"

#define FRAME_PIPELINE_STORES 3U
#define FRAME_PIPELINE_ROI_SIDE 256U

typedef struct {
    XAxiVdma dma;
    XScuGic gic;
    u32 frame_addr[FRAME_PIPELINE_STORES];
    u32 frame_bytes;
    u16 width;
    u16 height;
    u16 irq_id;
    volatile u32 irq_frames;
    volatile u32 irq_errors;
    volatile u32 last_error;
    volatile u32 last_write_index;
    u32 copied_frames;
    u32 dropped_frames;
    u32 gray_triplet_errors;
    u32 observed_slots;
    u32 recoveries;
} FramePipeline;

typedef struct {
    u32 sequence;
    u32 frame_index;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
} FrameRoiInfo;

int frame_pipeline_start(FramePipeline *pipeline, u16 width, u16 height,
                         u32 gray_base, u16 dma_device_id, u16 irq_id);
int frame_pipeline_copy_center_roi(FramePipeline *pipeline, u8 *destination,
                                   u32 destination_capacity, u32 *last_sequence,
                                   FrameRoiInfo *info);
int frame_pipeline_recover(FramePipeline *pipeline);

#endif
