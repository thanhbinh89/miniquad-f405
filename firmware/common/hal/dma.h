/**
 * @file    dma.h
 * @brief   STM32F4 DMA stream helpers (header only).
 *
 * Interrupt flags of streams 0-3 live in LISR/LIFCR, streams 4-7 in
 * HISR/HIFCR, at bit offsets 0, 6, 16 and 22 within the register.
 */
#ifndef DMA_H
#define DMA_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"

#define DMA_FLAG_FE     (1u << 0)
#define DMA_FLAG_DME    (1u << 2)
#define DMA_FLAG_TE     (1u << 3)
#define DMA_FLAG_HT     (1u << 4)
#define DMA_FLAG_TC     (1u << 5)
#define DMA_FLAG_ALL    (DMA_FLAG_FE | DMA_FLAG_DME | DMA_FLAG_TE | DMA_FLAG_HT | DMA_FLAG_TC)

static inline uint32_t dma_flag_shift(uint32_t stream_index)
{
    static const uint8_t shift[4] = { 0u, 6u, 16u, 22u };
    return shift[stream_index & 3u];
}

/** Read the (FE/DME/TE/HT/TC) flags of a stream, normalised to DMA_FLAG_*. */
static inline uint32_t dma_flags(const DMA_TypeDef *dma, uint32_t stream_index)
{
    uint32_t reg = (stream_index < 4u) ? dma->LISR : dma->HISR;
    return (reg >> dma_flag_shift(stream_index)) & DMA_FLAG_ALL;
}

static inline void dma_clear_flags(DMA_TypeDef *dma, uint32_t stream_index, uint32_t flags)
{
    uint32_t v = (flags & DMA_FLAG_ALL) << dma_flag_shift(stream_index);
    if (stream_index < 4u) {
        dma->LIFCR = v;
    } else {
        dma->HIFCR = v;
    }
}

/** Disable a stream and wait until the hardware has really stopped it. */
static inline void dma_stream_disable(DMA_Stream_TypeDef *s)
{
    s->CR &= ~DMA_SxCR_EN;
    while ((s->CR & DMA_SxCR_EN) != 0u) {
    }
}

/** CHSEL field for SxCR. */
#define DMA_CR_CHSEL(ch)    (((uint32_t)(ch) & 7u) << DMA_SxCR_CHSEL_Pos)

#endif /* DMA_H */
