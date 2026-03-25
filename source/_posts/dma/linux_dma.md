---
title: Linux DMA Engine Overview
date: 2026-03-19
toc: true
max_depth: 3 
mermaid: true
categories: dma
tags: [linux, dma]
---

# 简介
本文介绍linux内核下的DMA Engine概述，cyclic传输，以及DMA_PREP_FENCE这类传输flag。讨论dma framework，以及provider驱动之间的一些共性。

# Linux DMA Engine Overview

  1. What is DMA Engine?

  The DMA Engine is a Linux kernel subsystem that provides a standardized framework for DMA (Direct Memory Access) controllers. It allows device drivers to offload memory copy, memset, and scatter-gather operations to dedicated DMA hardware without CPU intervention.

  2. Architecture Components
```text
  +------------------+     +------------------+
  |   Client Driver  |     |   Client Driver  |
  +------------------+     +------------------+
             |                     |
             v                     v
  +------------------------------------------+
  |           DMA Engine Core                |
  |  (dmaengine.c, dma_cookie.c, etc.)       |
  +------------------------------------------+
             |
             v
  +------------------------------------------+
  |         DMA Controller Driver            |
  |    (Hardware-specific implementation)    |
  +------------------------------------------+
             |
             v
  +------------------------------------------+
  |           DMA Hardware                   |
  +------------------------------------------+
```
  3. Key Data Structures
```c
  // Main DMA channel structure
  struct dma_chan {
      struct dma_device *device;
      void *private;
      int chan_id;
      struct kobject kobj;
      // ...
  };

  // DMA device (controller) structure
  struct dma_device {
      unsigned int chancnt;
      struct list_head channels;
      struct list_head global_node;
      dma_cap_mask_t cap_mask;
      struct device *dev;
      // Callback operations
      struct dma_async_tx_descriptor *(*device_prep_dma_memcpy)(...);
      int (*device_tx_submit)(struct dma_async_tx_descriptor *tx);
      // ...
  };

  // Async transaction descriptor
  struct dma_async_tx_descriptor {
      dma_cookie_t cookie;
      enum dma_ctrl_flags flags;
      struct dma_chan *chan;
      void (*callback)(void *callback_param);
      void *callback_param;
      // ...
  };
```
  4. Key APIs

  For Client Drivers:
```text
  ┌─────────────────────────────┬────────────────────────────────┐
  │             API             │            Purpose             │
  ├─────────────────────────────┼────────────────────────────────┤
  │ dma_request_chan()          │ Request a DMA channel          │
  ├─────────────────────────────┼────────────────────────────────┤
  │ dma_release_channel()       │ Release a DMA channel          │
  ├─────────────────────────────┼────────────────────────────────┤
  │ dmaengine_prep_dma_memcpy() │ Prepare a memcpy transaction   │
  ├─────────────────────────────┼────────────────────────────────┤
  │ dmaengine_submit()          │ Submit transaction to hardware │
  ├─────────────────────────────┼────────────────────────────────┤
  │ dma_async_issue_pending()   │ Start pending transactions     │
  ├─────────────────────────────┼────────────────────────────────┤
  │ dma_async_is_tx_complete()  │ Check transaction status       │
  └─────────────────────────────┴────────────────────────────────┘
  For DMA Controller Drivers:
  ┌───────────────────────────────┬───────────────────────┐
  │              API              │        Purpose        │
  ├───────────────────────────────┼───────────────────────┤
  │ dma_async_device_register()   │ Register DMA device   │
  ├───────────────────────────────┼───────────────────────┤
  │ dma_async_device_unregister() │ Unregister DMA device │
  ├───────────────────────────────┼───────────────────────┤
  │ dma_channel_register()        │ Register a channel    │
  └───────────────────────────────┴───────────────────────┘
```
  5. How It Works - Typical Flow
```c
  // Step 1: Request channel
  struct dma_chan *chan = dma_request_chan(dev, "rx");

  // Step 2: Prepare transaction
  struct dma_async_tx_descriptor *tx;
  tx = dmaengine_prep_dma_memcpy(chan, dst_addr, src_addr,
                                  len, DMA_CTRL_ACK);

  // Step 3: Set callback (optional)
  tx->callback = my_callback;
  tx->callback_param = my_data;

  // Step 4: Submit transaction
  dma_cookie_t cookie = dmaengine_submit(tx);

  // Step 5: Issue pending transactions
  dma_async_issue_pending(chan);

  // Step 6: Wait for completion (polling or callback)
  enum dma_status status = dma_async_is_tx_complete(chan, cookie, ...);

  // Step 7: Release channel when done
  dma_release_channel(chan);
```
  6. DMA Capabilities

  The DMA Engine supports multiple capabilities:
  - DMA_MEMCPY - Memory to memory copy
  - DMA_MEMSET - Memory fill/set
  - DMA_SLAVE - Slave/device DMA (peripheral to memory)
  - DMA_CYCLIC - Cyclic DMA for streaming (audio, etc.)
  - DMA_XOR - XOR operations (RAID)
  - DMA_PQ - P+Q calculations (RAID6)

  7. Transfer Directions (for Slave DMA)

  - DMA_MEM_TO_MEM - Memory to memory
  - DMA_MEM_TO_DEV - Memory to device
  - DMA_DEV_TO_MEM - Device to memory
  - DMA_DEV_TO_DEV - Device to device



# DMA_CYCLIC

● DMA_CYCLIC - Cyclic DMA for Streaming

  What is Cyclic DMA?

  Cyclic DMA is a special DMA mode where the DMA controller automatically loops through a series of buffer descriptors in a circular fashion, continuously transferring data without CPU intervention after the initial setup.
  Key Characteristics
```text

  ┌──────────────────────┬─────────────────────────────────────────┐
  │       Feature        │               Description               │
  ├──────────────────────┼─────────────────────────────────────────┤
  │ Circular Buffer      │ Descriptors form a ring/loop            │
  ├──────────────────────┼─────────────────────────────────────────┤
  │ Continuous Operation │ Runs indefinitely until stopped         │
  ├──────────────────────┼─────────────────────────────────────────┤
  │ Periodic Callbacks   │ Can interrupt at segment boundaries     │
  ├──────────────────────┼─────────────────────────────────────────┤
  │ No Resubmission      │ No need to constantly requeue transfers │
  └──────────────────────┴─────────────────────────────────────────┘
```
  Architecture
```text
       ┌─────────────────────────────────────────┐
       │                                         │
       ▼                                         │
  ┌─────────┐    ┌─────────┐    ┌─────────┐      │
  │ Segment │───▶│ Segment │───▶│ Segment │──────┘
  │    0    │    │    1    │    │    2    │
  │ (period)│    │ (period)│    │ (period)│
  └─────────┘    └─────────┘    └─────────┘
        │              │              │
        ▼              ▼              ▼
     IRQ?           IRQ?           IRQ?
    (optional)    (optional)    (optional)
```
  Typical Use Cases

  1. Audio Playback (I2S, S/PDIF)
    - Continuous stream of audio samples to DAC
    - Each period generates an interrupt for ALSA to refill next period
  2. Audio Capture
    - Continuous stream from ADC to memory
    - Each period notifies application to consume data
  3. Serial Communication (UART, SPI)
    - Continuous RX/TX ring buffers
  4. PWM Generation
    - Cyclic waveform patterns

  How It Works - Audio Playback Example
```text
  Application → ALSA → ASoC → DMA Engine → DMA Hardware → I2S → Codec → Speakers
                      ↓
                Periodic IRQ
                      ↓
                Refill next period
```
  API Usage
```c
  // Step 1: Request DMA channel
  struct dma_chan *chan = dma_request_chan(dev, "tx");

  // Step 2: Configure slave parameters
  struct dma_slave_config cfg = {0};
  cfg.direction = DMA_MEM_TO_DEV;
  cfg.dst_addr = i2s_fifo_addr;
  cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
  cfg.src_maxburst = 4;  // Burst size
  dmaengine_slave_config(chan, &cfg);

  // Step 3: Prepare cyclic transfer
  // Buffer: physically contiguous, divided into periods
  dma_addr_t buf_addr = dma_map_single(dev, buf, buf_size, DMA_TO_DEVICE);

  struct dma_async_tx_descriptor *desc;
  desc = dmaengine_prep_dma_cyclic(
      chan,           // DMA channel
      buf_addr,       // Buffer DMA address
      buf_size,       // Total buffer size
      period_size,    // Size of each period (segment)
      DMA_MEM_TO_DEV, // Direction
      DMA_CTRL_ACK    // Flags
  );

  // Step 4: Set callback (called at each period boundary)
  desc->callback = audio_period_callback;
  desc->callback_param = substream;  // ALSA substream

  // Step 5: Submit and start
  dma_cookie_t cookie = dmaengine_submit(desc);
  dma_async_issue_pending(chan);

  // DMA now runs continuously, calling callback every period_size bytes

  // Step 6: To stop
  dmaengine_terminate_all(chan);  // or dmaengine_terminate_sync(chan)

  // Step 7: Release
  dma_release_channel(chan);

  Callback Behavior

```c
  static void audio_period_callback(void *param)
  {
      struct snd_pcm_substream *substream = param;

      // Called at each period boundary
      // ALSA updates its hardware pointer
      snd_pcm_period_elapsed(substream);

      // No need to resubmit - DMA continues to next period automatically
  }
```
  Buffer Layout Example
```text
  For a 4-period audio buffer:

  Time ─────────────────────────────────────▶

  Buffer:  [ Period 0 ][ Period 1 ][ Period 2 ][ Period 3 ]
                │          │          │          │
                ▼          ▼          ▼          ▼
  IRQ:         ●──────────●──────────●──────────●──────────▶

  DMA Head:    ───────────────────────► (wraps to Period 0)
```
  Key Differences: Cyclic vs. Scatter-Gather
```text
  ┌──────────────┬──────────────────────────┬────────────────────────────┐
  │    Aspect    │        Cyclic DMA        │     Scatter-Gather DMA     │
  ├──────────────┼──────────────────────────┼────────────────────────────┤
  │ Pattern      │ Circular, repeating      │ Linear, one-shot           │
  ├──────────────┼──────────────────────────┼────────────────────────────┤
  │ Duration     │ Continuous until stopped │ Single transfer            │
  ├──────────────┼──────────────────────────┼────────────────────────────┤
  │ CPU Overhead │ Very low (no resubmit)   │ Higher (resubmit needed)   │
  ├──────────────┼──────────────────────────┼────────────────────────────┤
  │ Use Case     │ Streaming (audio, UART)  │ Block transfers (file I/O) │
  ├──────────────┼──────────────────────────┼────────────────────────────┤
  │ Interrupts   │ Periodic (optional)      │ End-of-transfer            │
  └──────────────┴──────────────────────────┴────────────────────────────┘
```
  Important Considerations

  1. Buffer Must Be DMA-Coherent
```c
  // Use coherent allocation for cyclic buffers
  void *buf = dma_alloc_coherent(dev, size, &dma_handle, GFP_KERNEL);
```
  2. Period Size Matters
    - Smaller periods = more frequent interrupts = lower latency but higher CPU
    - Larger periods = fewer interrupts = higher latency but less CPU overhead
  3. No Rewind
    - Once started, cyclic DMA flows forward continuously
    - To "rewind", you must terminate and restart
  4. Memory Must Be Contiguous
    - Cyclic DMA typically requires physically contiguous buffer
    - Cannot use standard dma_map_sg() with scattered pages

  ALSA Integration Example
```c
  // In ASoC PCM driver
  static int snd_soc_pcm_lib_hw_params(struct snd_pcm_substream *substream,
                                       struct snd_pcm_hw_params *params)
  {
      struct snd_soc_pcm_runtime *rtd = substream->private_data;
      struct snd_dma_buffer *dma_buf = &substream->dma_buffer;

      // Allocate DMA buffer
      dma_buf->area = dma_alloc_coherent(rtd->card->dev,
                                          params_buffer_bytes(params),
                                          &dma_buf->addr,
                                          GFP_KERNEL);
      dma_buf->bytes = params_buffer_bytes(params);

      return 0;
  }

  static int snd_soc_pcm_lib_trigger(struct snd_pcm_substream *substream, int cmd)
  {
      switch (cmd) {
      case SNDRV_PCM_TRIGGER_START:
          dma_async_issue_pending(chan);
          break;
      case SNDRV_PCM_TRIGGER_STOP:
          dmaengine_terminate_all(chan);
          break;
      }
      return 0;
  }
```
  Common DMA Controller Drivers Supporting Cyclic

  - dw_dmac - DesignWare DMA
  - pl330 - ARM PrimeCell PL330
  - imx-sdma - i.MX SDMA
  - omap-dma - OMAP DMA
  - stm32-dma - STM32 DMA
  - xilinx-dma - Xilinx AXI DMA

  ---
  Want me to explain any specific aspect in more detail, such as the hardware implementation or how to debug cyclic DMA issues?

✻ Cooked for 52s

❯ the code you showed to me:
  ⎿  Interrupted · What should Claude do instead?

❯ the code you showed to me:
  dma_addr_t buf_addr = dma_map_single(dev, buf, buf_size, DMA_TO_DEVICE);

  struct dma_async_tx_descriptor *desc;
  desc = dmaengine_prep_dma_cyclic(
  chan,           // DMA channel
  buf_addr,       // Buffer DMA address
  buf_size,       // Total buffer size
  period_size,    // Size of each period (segment)
  DMA_MEM_TO_DEV, // Direction
  DMA_CTRL_ACK    // Flags
  );

# dmaengine_prep_dma_cyclic

  how to understand the dmaengine_prep_dma_cyclic? this api will split the continous memory which from addr "buf_addr"
  start, into each segment in length of "period" ?

● Yes, exactly! Let me clarify how dmaengine_prep_dma_cyclic() works:

  Understanding dmaengine_prep_dma_cyclic()

  What It Does
```text
  Continuous Buffer (buf_size bytes)
  ┌─────────────────────────────────────────────────────────┐
  │                                                         │
  ▼                                                         │
  buf_addr                                                  │
  ┌─────────────┬─────────────┬─────────────┬─────────────┐ │
  │  Period 0   │  Period 1   │  Period 2   │  Period 3   │ │
  │ (period_sz) │ (period_sz) │ (period_sz) │ (period_sz) │ │
  └─────────────┴─────────────┴─────────────┴─────────────┘ │
            ▲                                               │
            └────────────── wraps around ───────────────────┘
```
  How the Memory is Split
```c
  // Example values
  buf_size    = 4096 bytes   // Total buffer
  period_size = 1024 bytes   // Each segment
  // Result: 4 periods (4096 / 1024 = 4)

  // Internally, the DMA engine driver creates descriptors like:
  struct dma_cyclic_desc {
      // Period 0: buf_addr + 0
      // Period 1: buf_addr + period_size
      // Period 2: buf_addr + 2 * period_size
      // Period 3: buf_addr + 3 * period_size
      // Then wraps back to Period 0
  };
```

  What Happens Inside the API
```c
  desc = dmaengine_prep_dma_cyclic(
      chan,
      buf_addr,       // Start of entire buffer
      buf_size,       // Total: 4096 bytes
      period_size,    // Each period: 1024 bytes
      DMA_MEM_TO_DEV,
      DMA_CTRL_ACK
  );
```
  Internally, the DMA subsystem:

  1. Calculates number of periods: num_periods = buf_size / period_size
  2. Creates a chain of DMA descriptors (one per period)
  3. Links the last descriptor back to the first (circular)
  4. Each descriptor transfers period_size bytes
  5. Returns a single "master" descriptor to represent the whole cyclic transfer

  Descriptor Chain Structure

  // Conceptual internal structure:
```c
  struct dma_async_tx_descriptor {
      struct dma_cyclic_chunk {
          dma_addr_t addr = buf_addr + 0;           // Period 0
          u32 len = period_size;
          struct dma_cyclic_chunk *next = &chunk1;
      } chunk0;

      struct dma_cyclic_chunk {
          dma_addr_t addr = buf_addr + 1024;        // Period 1
          u32 len = period_size;
          struct dma_cyclic_chunk *next = &chunk2;
      } chunk1;

      struct dma_cyclic_chunk {
          dma_addr_t addr = buf_addr + 2048;        // Period 2
          u32 len = period_size;
          struct dma_cyclic_chunk *next = &chunk3;
      } chunk2;

      struct dma_cyclic_chunk {
          dma_addr_t addr = buf_addr + 3072;        // Period 3
          u32 len = period_size;
          struct dma_cyclic_chunk *next = &chunk0;  // ← WRAPS BACK!
      } chunk3;
  };
```
  Important Constraints

```c
  // buf_size MUST be a multiple of period_size
  buf_size % period_size == 0;  // e.g., 4096 % 1024 = 0

  // If not, the API may fail or behave incorrectly
```

  Callback Timing

```c
  desc->callback = audio_period_callback;
  // Callback fires at EACH period boundary:
  //
  // Time ──────────────────────────────────────────▶
  //
  // DMA:   [P0][P1][P2][P3][P0][P1][P2][P3][P0]...
  //         │   │   │   │   │   │   │   │   │
  // IRQ:   ●───●───●───●───●───●───●───●───●───▶
  //
  // Each ● = callback invoked
```
  Visual Summary

  dmaengine_prep_dma_cyclic() takes:
```text
      buf_addr ──▶ [||||||||||||||||]  (contiguous memory)
                     │      │
                     │      └── period_size
                     │
                     └── buf_size (total)
```
  And creates internally:
```text
      ┌──────────────────────────────────────┐
      │  [P0]──▶[P1]──▶[P2]──▶[P3]──┐        │
      │   ▲                        │        │
      │   └────────────────────────┘ (loop) │
      │                                      │
      │  Each period = period_size bytes     │
      │  Callback at each period boundary    │
      └──────────────────────────────────────┘
```
  Your Understanding is Correct

  this api will split the continous memory which from addr "buf_addr" start, into each segment in length of "period" ?

  Yes! The API:
  1. Takes one contiguous buffer starting at buf_addr
  2. Logically divides it into buf_size / period_size segments
  3. Creates a circular descriptor chain where each segment transfers period_size bytes
  4. The hardware automatically loops through all segments forever (until terminated)

# DMA_PREP_FENCE

tell me about flag "DMA_PREP_FENCE"

● DMA_PREP_FENCE Flag

  What It Does

  DMA_PREP_FENCE is a flag used with dmaengine_prep_*() functions to **enforce memory ordering constraints**. It tells the DMA engine to act as a memory barrier (fence) that ensures all previous memory operations are completed before this DMA transaction begins.

  The Flag Definition
```c
  // In include/linux/dmaengine.h
  #define DMA_PREP_FENCE  (1 << 4)

  // Used in the flags parameter:
  desc = dmaengine_prep_dma_memcpy(chan, dst, src, len,
                                    DMA_CTRL_ACK | DMA_PREP_FENCE);
```
  Why It's Needed - The Problem

  Without DMA_PREP_FENCE, DMA transactions may be reordered for performance:
```text
  CPU writes to memory:
  ┌─────────────┐
  │ Write A     │
  │ Write B     │
  │ Write C     │
  └─────────────┘
       │
       ▼
  DMA Engine Queue (without fence):
  ┌─────────────┐
  │ Transfer C  │  ← May execute first!
  │ Transfer A  │
  │ Transfer B  │
  └─────────────┘
```
  With DMA_PREP_FENCE:
```text
  DMA Engine Queue (with fence):
  ┌─────────────┐
  │ Transfer A  │  ← Must complete first
  ├─────────────┤
  │ FENCE       │  ← Memory barrier
  ├─────────────┤
  │ Transfer B  │  ← Waits for all prior ops
  │ Transfer C  │
  └─────────────┘
```
  Typical Use Cases

  1. Synchronization Between DMA Channels
```c
  // Channel 1: DMA memcpy
  struct dma_async_tx_descriptor *tx1;
  tx1 = dmaengine_prep_dma_memcpy(chan1, dst1, src1, len, DMA_CTRL_ACK);
  dmaengine_submit(tx1);

  // Channel 2: Must wait for channel 1 to complete
  struct dma_async_tx_descriptor *tx2;
  tx2 = dmaengine_prep_dma_memcpy(chan2, dst2, dst1, len,
                                   DMA_CTRL_ACK | DMA_PREP_FENCE);
  tx2->dep = tx1;  // Dependency on tx1
  dmaengine_submit(tx2);
```
  2. Coherent Data Processing Pipeline
```c
  // Step 1: DMA from device to buffer
  desc1 = dmaengine_prep_dma_single(chan, buf, dev_addr, len,
                                     DMA_DEV_TO_MEM, 0);

  // Step 2: CPU or DMA processes data (e.g., crypto, checksum)
  // ...

  // Step 3: DMA from buffer to device - MUST wait for step 2
  desc2 = dmaengine_prep_dma_single(chan, dev_addr, buf, len,
                                     DMA_MEM_TO_DEV,
                                     DMA_CTRL_ACK | DMA_PREP_FENCE);
```
  How It Works Internally
```c
  // Conceptual implementation in DMA controller driver

  static struct dma_async_tx_descriptor *
  my_dma_prep_dma_memcpy(struct dma_chan *chan,
                         dma_addr_t dest, dma_addr_t src,
                         size_t len, unsigned long flags)
  {
      struct my_dma_desc *desc = to_my_desc(chan);

      // Allocate hardware descriptor
      desc->hw_desc.src = src;
      desc->hw_desc.dst = dest;
      desc->hw_desc.len = len;

      if (flags & DMA_PREP_FENCE) {
          // Set hardware fence bit
          desc->hw_desc.ctrl |= MY_DMA_CTRL_FENCE;

          // Or wait for previous descriptors to complete
          my_dma_wait_for_pending(chan);
      }

      return &desc->async_tx;
  }
```
  Relationship with Other Flags
```text
  ┌────────────────────┬────────────────────────────────────────────┐
  │        Flag        │                  Purpose                   │
  ├────────────────────┼────────────────────────────────────────────┤
  │ DMA_CTRL_ACK       │ Acknowledge transaction completion         │
  ├────────────────────┼────────────────────────────────────────────┤
  │ DMA_PREP_FENCE     │ Insert memory barrier before this transfer │
  ├────────────────────┼────────────────────────────────────────────┤
  │ DMA_PREP_LOAD_EOT  │ Load descriptor at End Of Transfer         │
  ├────────────────────┼────────────────────────────────────────────┤
  │ DMA_PREP_INTERRUPT │ Generate interrupt on completion           │
  ├────────────────────┼────────────────────────────────────────────┤
  │ DMA_CTRL_REUSE     │ Allow descriptor reuse                     │
  └────────────────────┴────────────────────────────────────────────┘
```
  Example: Chained Transfers with Fence
```c
  // Transfer 1: No fence, can be reordered
  desc1 = dmaengine_prep_dma_memcpy(chan, dst1, src1, len, DMA_CTRL_ACK);
  cookie1 = dmaengine_submit(desc1);

  // Transfer 2: No fence, can be reordered
  desc2 = dmaengine_prep_dma_memcpy(chan, dst2, src2, len, DMA_CTRL_ACK);
  cookie2 = dmaengine_submit(desc2);

  // Transfer 3: FENCE - must wait for desc1 and desc2 to complete
  desc3 = dmaengine_prep_dma_memcpy(chan, dst3, src3, len,
                                     DMA_CTRL_ACK | DMA_PREP_FENCE);
  desc3->dependency = desc2;  // Explicit dependency
  cookie3 = dmaengine_submit(desc3);

  dma_async_issue_pending(chan);

  Hardware Support

  Not all DMA controllers support DMA_PREP_FENCE. Check the driver:

```c
  // In DMA controller driver's capability check
  if (dma_dev->cap_mask & DMA_CAP_FENCE) {
      // Hardware supports fencing
  } else {
      // Fence flag ignored, software must handle ordering
  }
```
  Common Controllers Supporting Fence
```text
  ┌───────────────────┬───────────────────┐
  │    Controller     │   Fence Support   │
  ├───────────────────┼───────────────────┤
  │ ARM PL330         │ Yes (via SEV/WFE) │
  ├───────────────────┼───────────────────┤
  │ DesignWare DW_DMA │ Yes               │
  ├───────────────────┼───────────────────┤
  │ Xilinx AXI DMA    │ Yes (via SYNC)    │
  ├───────────────────┼───────────────────┤
  │ Intel IDMA        │ Yes               │
  ├───────────────────┼───────────────────┤
  │ STM32 DMA         │ Limited           │
  └───────────────────┴───────────────────┘
```
  Alternative: Explicit Dependencies

  If hardware doesn't support DMA_PREP_FENCE, use explicit dependencies:
```c
  // Instead of DMA_PREP_FENCE, chain descriptors
  desc1 = dmaengine_prep_dma_memcpy(...);
  desc2 = dmaengine_prep_dma_memcpy(...);

  // Link them explicitly
  desc2->dep = desc1;  // desc2 waits for desc1

  dmaengine_submit(desc1);
  dmaengine_submit(desc2);
```
  ---

# code segments

```c
struct dma_async_tx_descriptor {
	dma_cookie_t cookie;
	enum dma_ctrl_flags flags; /* not a 'long' to pack with cookie */
	dma_addr_t phys;
	struct dma_chan *chan;
	dma_cookie_t (*tx_submit)(struct dma_async_tx_descriptor *tx);
	int (*desc_free)(struct dma_async_tx_descriptor *tx);
	dma_async_tx_callback callback;
	dma_async_tx_callback_result callback_result;
	void *callback_param;
	struct dmaengine_unmap_data *unmap;
	enum dma_desc_metadata_mode desc_metadata_mode;
	struct dma_descriptor_metadata_ops *metadata_ops;
#ifdef CONFIG_ASYNC_TX_ENABLE_CHANNEL_SWITCH
	struct dma_async_tx_descriptor *next;
	struct dma_async_tx_descriptor *parent;
	spinlock_t lock;
#endif
};

static inline struct dma_async_tx_descriptor *txd_next(struct dma_async_tx_descriptor *txd)
{
	return txd->next;
}

/**
 * dma_run_dependencies - process dependent operations on the target channel
 * @tx:		transaction with dependencies
 *
 * Helper routine for DMA drivers to process (start) dependent operations
 * on their target channel.
 */
void dma_run_dependencies(struct dma_async_tx_descriptor *tx)
{
	struct dma_async_tx_descriptor *dep = txd_next(tx);
	struct dma_async_tx_descriptor *dep_next;
	struct dma_chan *chan;

	if (!dep)
		return;

	/* we'll submit tx->next now, so clear the link */
	txd_clear_next(tx);
	chan = dep->chan;

	/* keep submitting up until a channel switch is detected
	 * in that case we will be called again as a result of
	 * processing the interrupt from async_tx_channel_switch
	 */
	for (; dep; dep = dep_next) {
		txd_lock(dep);
		txd_clear_parent(dep);
		dep_next = txd_next(dep);
		if (dep_next && dep_next->chan == chan)
			txd_clear_next(dep); /* ->next will be submitted */
		else
			dep_next = NULL; /* submit current dep and terminate */
		txd_unlock(dep);

		dep->tx_submit(dep);
	}

	chan->device->device_issue_pending(chan);
}
EXPORT_SYMBOL_GPL(dma_run_dependencies);
```


  Summary: DMA_PREP_FENCE ensures memory ordering by acting as a barrier - all previous DMA/memory operations must complete before this transfer starts. Essential for data pipelines where transfer order matters.

# the provider for cyclic api

❯ you really a expert in dma field, so i have some more profund questions:
  1. the api like dmaengine_prep_dma_cyclic is consumer api, the corresponeding provider api is platform-related

● You're asking excellent architectural questions! Let me clarify the consumer vs provider API split in the DMA Engine framework.

  DMA Engine API Layers
```text
  ┌─────────────────────────────────────────────────────────┐
  │                    Consumer Side                        │
  │  (Device drivers that USE DMA: audio, network, spi...)  │
  │                                                         │
  │  dmaengine_prep_dma_cyclic()  ← You are here           │
  │  dma_request_chan()                                     │
  │  dmaengine_submit()                                     │
  │  dma_async_issue_pending()                              │
  └─────────────────────────────────────────────────────────┘
                            │
                            ▼
  ┌─────────────────────────────────────────────────────────┐
  │                    DMA Engine Core                      │
  │  (Generic framework: dmaengine.c, dma_cookie.c...)      │
  │                                                         │
  │  - Channel allocation/allocation                        │
  │  - Cookie management                                    │
  │  - Descriptor chaining                                  │
  │  - Capability negotiation                               │
  └─────────────────────────────────────────────────────────┘
                            │
                            ▼
  ┌─────────────────────────────────────────────────────────┐
  │                    Provider Side                        │
  │  (DMA Controller Drivers: platform-specific)            │
  │                                                         │
  │  device_prep_dma_cyclic  ← Provider implements this    │
  │  device_tx_submit                                        │
  │  device_issue_pending                                    │
  │  device_terminate_all                                    │
  └─────────────────────────────────────────────────────────┘
                            │
                            ▼
  ┌─────────────────────────────────────────────────────────┐
  │                 DMA Hardware (SoC)                      │
  │  (PL330, DesignWare, STM32-DMA, i.MX-SDMA...)           │
  └─────────────────────────────────────────────────────────┘
```
  ---
  1. Consumer API (Generic, Platform-Independent)
```c
  // include/linux/dmaengine.h

  /* Request a channel */
  struct dma_chan *dma_request_chan(struct device *dev, const char *name);

  /* Prepare cyclic transfer */
  struct dma_async_tx_descriptor *
  dmaengine_prep_dma_cyclic(struct dma_chan *chan,
                            dma_addr_t buf_addr,
                            size_t buf_len,
                            size_t period_len,
                            enum dma_transfer_direction direction,
                            unsigned long flags);

  /* Submit and start */
  dma_cookie_t dmaengine_submit(struct dma_async_tx_descriptor *tx);
  void dma_async_issue_pending(struct dma_chan *chan);

  /* Check completion */
  enum dma_status dma_async_is_tx_complete(struct dma_chan *chan,
                                           dma_cookie_t cookie,
                                           dma_cookie_t *last,
                                           dma_cookie_t *used);
  /* Terminate */
  int dmaengine_terminate_all(struct dma_chan *chan);
```

  Location: drivers/dma/dmaengine.c

  Characteristics:
  - Same API across all platforms
  - Device drivers (ALSA, network, SPI) use these
  - Never touches hardware registers directly

  ---
  1. Provider API (Platform-Specific, Hardware Operations)
```c
  // include/linux/dmaengine.h - struct dma_device

  struct dma_device {
      struct list_head global_node;
      struct device *dev;

      /* === Provider MUST implement these callbacks === */

      // Channel operations
      struct dma_chan *(*device_channel_allocate)(struct dma_device *dev);
      int (*device_free_channel)(struct dma_chan *chan);

      // Transfer preparation - THE KEY PROVIDER APIs
      struct dma_async_tx_descriptor *(*device_prep_dma_cyclic)(
          struct dma_chan *chan,
          dma_addr_t buf_addr,
          size_t buf_len,
          size_t period_len,
          enum dma_transfer_direction direction,
          unsigned long flags,
          void *context);

      struct dma_async_tx_descriptor *(*device_prep_dma_memcpy)(
          struct dma_chan *chan,
          dma_addr_t dest,
          dma_addr_t src,
          size_t len,
          unsigned long flags);

      // Submit transaction
      dma_cookie_t (*device_tx_submit)(struct dma_async_tx_descriptor *tx);

      // Start transfers
      void (*device_issue_pending)(struct dma_chan *chan);

      // Control/Status
      int (*device_config)(struct dma_chan *chan,
                           struct dma_slave_config *config);
      int (*device_terminate_all)(struct dma_chan *chan);
      enum dma_status (*device_tx_status)(struct dma_chan *chan,
                                          dma_cookie_t cookie,
                                          struct dma_tx_state *txstate);

      /* ... many more callbacks ... */
  };
```
  Location: Implemented in each DMA controller driver

  Examples:
```text
  ┌─────────────────┬─────────────────────────────────┐
  │ Provider Driver │            File Path            │
  ├─────────────────┼─────────────────────────────────┤
  │ ARM PL330       │ drivers/dma/pl330.c             │
  ├─────────────────┼─────────────────────────────────┤
  │ DesignWare      │ drivers/dma/dw/core.c           │
  ├─────────────────┼─────────────────────────────────┤
  │ STM32 DMA       │ drivers/dma/stm32-dma.c         │
  ├─────────────────┼─────────────────────────────────┤
  │ i.MX SDMA       │ drivers/dma/imx-sdma.c          │
  ├─────────────────┼─────────────────────────────────┤
  │ Xilinx AXI DMA  │ drivers/dma/xilinx/xilinx_dma.c │
  └─────────────────┴─────────────────────────────────┘
```
  ---
  3. Example: How dmaengine_prep_dma_cyclic() Calls Provider

  // Consumer calls this (drivers/dma/dmaengine.c)
```c
  struct dma_async_tx_descriptor *
  dmaengine_prep_dma_cyclic(struct dma_chan *chan,
                            dma_addr_t buf_addr,
                            size_t buf_len,
                            size_t period_len,
                            enum dma_transfer_direction direction,
                            unsigned long flags)
  {
      /* Sanity checks */
      if (!chan || !chan->device)
          return NULL;

      /* Check capability */
      if (!dma_chan_has_cap(chan, DMA_SLAVE))
          return NULL;

      /* === CALL PROVIDER IMPLEMENTATION === */
      return chan->device->device_prep_dma_cyclic(
          chan, buf_addr, buf_len, period_len,
          direction, flags, NULL);
  }
  EXPORT_SYMBOL_GPL(dmaengine_prep_dma_cyclic);
```
  ---
  4. Provider Implementation Example (PL330)
```c
  // drivers/dma/pl330.c

  static struct dma_async_tx_descriptor *
  pl330_prep_dma_cyclic(struct dma_chan *chan,
                        dma_addr_t buf_addr,
                        size_t buf_len,
                        size_t period_len,
                        enum dma_transfer_direction direction,
                        unsigned long flags,
                        void *context)
  {
      struct pl330_chan *pch = to_pl330_chan(chan);
      struct pl330_desc *desc;
      struct pl330_xfer *xfer;
      int num_periods = buf_len / period_len;
      int i;

      /* Allocate descriptor chain */
      desc = pl330_get_desc(pch);
      if (!desc)
          return NULL;

      /* Build cyclic descriptor chain */
      for (i = 0; i < num_periods; i++) {
          xfer = &desc->tx.xfers[i];
          xfer->src_addr = (direction == DMA_MEM_TO_DEV) ?
                           buf_addr + (i * period_len) : pch->fifo_addr;
          xfer->dst_addr = (direction == DMA_MEM_TO_DEV) ?
                           pch->fifo_addr : buf_addr + (i * period_len);
          xfer->bytes = period_len;
      }

      /* Mark as cyclic - last descriptor loops back to first */
      desc->tx.cyclic = true;
      desc->tx.loop = true;

      /* Set callback at period boundaries */
      desc->tx.callback = NULL;  /* Will be set by consumer */

      return &desc->tx.desc;
  }


  /* Register with DMA Engine Core */
  static int pl330_probe(struct amba_device *adev, ...)
  {
      struct pl330_dmac *pl330 = ...;

      /* Setup provider callbacks */
      pl330->dmac.device_prep_dma_cyclic = pl330_prep_dma_cyclic;
      pl330->dmac.device_tx_submit = pl330_tx_submit;
      pl330->dmac.device_issue_pending = pl330_issue_pending;
      pl330->dmac.device_config = pl330_config;
      pl330->dmac.device_terminate_all = pl330_terminate_all;

      /* Register with framework */
      dma_async_device_register(&pl330->dmac);

      return 0;
  }
```

  ---
  5. Complete Flow Diagram
```text
  ┌─────────────────────────────────────────────────────────────────┐
  │  Consumer Driver (e.g., ASoC Audio)                             │
  │                                                                 │
  │  dmaengine_prep_dma_cyclic(chan, buf, len, period, ...)         │
  │                          │                                      │
  └──────────────────────────┼──────────────────────────────────────┘
                             │ Consumer API
                             ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  DMA Engine Core (drivers/dma/dmaengine.c)                      │
  │                                                                 │
  │  dmaengine_prep_dma_cyclic() {                                  │
  │      /* Generic checks */                                       │
  │      return chan->device->device_prep_dma_cyclic(...);  /* → */ │
  │  }                                                              │
  └──────────────────────────┼──────────────────────────────────────┘
                             │ Provider Callback
                             ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  Provider Driver (e.g., pl330.c, stm32-dma.c)                   │
  │                                                                 │
  │  pl330_prep_dma_cyclic() {                                      │
  │      /* Hardware-specific: */                                   │
  │      - Allocate HW descriptors                                  │
  │      - Program burst size, width                                │
  │      - Build descriptor chain                                   │
  │      - Set up interrupts                                        │
  │      return &desc->async_tx;                                    │
  │  }                                                              │
  └──────────────────────────┼──────────────────────────────────────┘
                             │
                             ▼
  ┌─────────────────────────────────────────────────────────────────┐
  │  DMA Hardware Registers                                         │
  │                                                                 │
  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐               │
  │  │  SAR    │ │  DAR    │  │  LLCR   │ │  CTRL   │  (PL330)    │
  │  │ 0x1000  │ │ 0x2000  │ │ 0x100   │ │ 0x0B    │              │
  │  └─────────┘ └─────────┘ └─────────┘ └─────────┘              │
  └─────────────────────────────────────────────────────────────────┘
```
  ---
  6. Summary Table
```text
  ┌──────────────────┬────────────────────────────────────────┬─────────────────────────────────┐
  │      Aspect      │              Consumer API              │          Provider API           │
  ├──────────────────┼────────────────────────────────────────┼─────────────────────────────────┤
  │ Who uses         │ Device drivers (ASoC, SPI, network)    │ DMA controller driver authors   │
  ├──────────────────┼────────────────────────────────────────┼─────────────────────────────────┤
  │ Location         │ drivers/dma/dmaengine.c                │ drivers/dma/<controller>.c      │
  ├──────────────────┼────────────────────────────────────────┼─────────────────────────────────┤
  │ Platform         │ Generic (same everywhere)              │ Hardware-specific               │
  ├──────────────────┼────────────────────────────────────────┼─────────────────────────────────┤
  │ Key functions    │ dmaengine_prep_*(), dma_request_chan() │ device_prep_*, device_tx_submit │
  ├──────────────────┼────────────────────────────────────────┼─────────────────────────────────┤
  │ Touches hardware │ No                                     │ Yes (register programming)      │
  ├──────────────────┼────────────────────────────────────────┼─────────────────────────────────┤
  │ Complexity       │ Simple, high-level                     │ Complex, hardware details       │
  └──────────────────┴────────────────────────────────────────┴─────────────────────────────────┘
```
  ---
  Your understanding is correct: dmaengine_prep_dma_cyclic() is the consumer-facing API. The provider implements device_prep_dma_cyclic callback which is hardware/platform-specific. The DMA Engine Core bridges these two layers.

  What's your next question about the DMA architecture?


# talk about dma provider common infrastructure

 so maybe there are some common logics for "Allocate HW descriptors" between the different hareware/platform implements?

● Yes! Excellent observation! There are common patterns and even shared helper code for descriptor allocation across different DMA controllers.

  Common Descriptor Allocation Patterns

  1. Generic Descriptor Management (DMA Engine Core)

  The DMA Engine Core provides the base infrastructure:

```c
  // include/linux/dmaengine.h
  struct dma_async_tx_descriptor {
      dma_cookie_t cookie;
      enum dma_ctrl_flags flags;
      struct list_head tx_list;
      struct dma_chan *chan;

      /* Callbacks */
      void (*callback)(void *callback_param);
      void *callback_param;

      /* Dependency tracking */
      struct dma_async_tx_descriptor *dep;
      struct list_head dependents;
  };

  // drivers/dma/dmaengine.c - Common allocation
  static void dma_async_tx_descriptor_init(struct dma_async_tx_descriptor *tx,
                                           struct dma_chan *chan)
  {
      tx->chan = chan;
      tx->flags = 0;
      tx->cookie = 0;
      INIT_LIST_HEAD(&tx->tx_list);
      INIT_LIST_HEAD(&tx->dependents);
  }
```

  2. Common Pattern: Embedded Descriptor Structure

  Most DMA controller drivers use this pattern:

```c
  /* === COMMON PATTERN ACROSS DRIVERS === */

  struct my_dma_desc {
      /* Embedded generic descriptor - MUST be first */
      struct dma_async_tx_descriptor async_tx;

      /* Driver-specific fields */
      struct list_head node;      /* Link to free/active list */
      struct my_dma_hw_desc *hw;  /* Hardware descriptor in DMA memory */
      dma_addr_t hw_addr;         /* DMA address of hw descriptor */
      struct my_dma_chan *chan;   /* Back pointer to channel */
      int cycles;                 /* For cyclic transfers */
      /* ... */
  };

  /* Common allocation helper */
  static struct my_dma_desc *my_dma_alloc_desc(struct my_dma_chan *my_chan)
  {
      struct my_dma_desc *desc;

      /* Get from pre-allocated pool (common optimization) */
      if (!list_empty(&my_chan->free_descs)) {
          desc = list_first_entry(&my_chan->free_descs,
                                  struct my_dma_desc, node);
          list_del(&desc->node);
          goto found;
      }

      /* Allocate new if pool empty */
      desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
      if (!desc)
          return NULL;

      /* Initialize generic part */
      dma_async_tx_descriptor_init(&desc->async_tx, &my_chan->chan);

      /* Set driver-specific callbacks */
      desc->async_tx.tx_submit = my_dma_tx_submit;
      desc->async_tx.callback = NULL;

  found:
      desc->chan = my_chan;
      return desc;
  }
```

  3. Real Example: DesignWare DMA (drivers/dma/dw/core.c)

```c
  /* Hardware descriptor structure */
  struct dw_desc {
      struct dw_lli *lli;         /* Linked List Item (HW format) */
      dma_addr_t lli_phys;        /* Physical addr of LLI */
      size_t len;                 /* Transfer length */
      struct list_head desc_node; /* Chain to other descriptors */

      /* Embed generic descriptor */
      struct dma_async_tx_descriptor tx_desc;
  };

  /* Channel structure */
  struct dw_dma_chan {
      struct dma_chan chan;       /* Generic channel */
      struct list_head free_desc_list;  /* Pool of free descriptors */
      struct list_head active_list;     /* Active descriptors */
      struct list_head queue;           /* Queued descriptors */
      spinlock_t lock;
      /* ... */
  };

  /* Common descriptor allocation */
  static struct dw_desc *dw_dma_get_desc(struct dw_dma_chan *dwc)
  {
      struct dw_desc *desc;

      /* Try to get from free list first (NO kmalloc!) */
      if (!list_empty(&dwc->free_desc_list)) {
          desc = list_first_entry(&dwc->free_desc_list,
                                  struct dw_desc, desc_node);
          list_del(&desc->desc_node);
          memset(desc->lli, 0, sizeof(struct dw_lli));  /* Clear HW part */
          return desc;
      }

      /* Allocate new descriptor + hardware LLI */
      desc = kzalloc(sizeof(struct dw_desc), GFP_NOWAIT);
      if (!desc)
          return NULL;

      /* Allocate hardware descriptor in DMA-coherent memory */
      desc->lli = dma_alloc_coherent(dwc->chan.device->dev,
                                      sizeof(struct dw_lli),
                                      &desc->lli_phys,
                                      GFP_NOWAIT);
      if (!desc->lli) {
          kfree(desc);
          return NULL;
      }

      /* Initialize generic part */
      dma_async_tx_descriptor_init(&desc->tx_desc, &dwc->chan);
      desc->tx_desc.tx_submit = dw_dma_tx_submit;

      return desc;
  }

  /* Common descriptor free (return to pool) */
  static void dw_dma_desc_free(struct dw_desc *desc)
  {
      list_add_tail(&desc->desc_node, &desc->chan->free_desc_list);
  }
```

  4. Real Example: STM32 DMA (drivers/dma/stm32-dma.c)

```c
  /* Hardware descriptor (STM32 format) */
  struct stm32_dma_desc {
      struct dma_async_tx_descriptor desc;
      struct list_head node;
      u32 chan_config;            /* Channel config register value */
      struct stm32_dma_sg_req *sg_req;  /* Scatter-gather list */
      int num_sgs;                /* Number of SG entries */
  };

  /* Common cyclic descriptor allocation */
  static struct stm32_dma_desc *
  stm32_dma_alloc_cyclic_desc(struct stm32_dma_chan *chan,
                              dma_addr_t buf_addr,
                              size_t buf_len,
                              size_t period_len)
  {
      struct stm32_dma_desc *desc;
      int num_periods = buf_len / period_len;
      int i;

      desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
      if (!desc)
          return NULL;

      /* Allocate SG entries for each period */
      desc->sg_req = kcalloc(num_periods, sizeof(*desc->sg_req), GFP_NOWAIT);
      if (!desc->sg_req) {
          kfree(desc);
          return NULL;
      }

      desc->num_sgs = num_periods;

      /* Build descriptor chain - COMMON PATTERN */
      for (i = 0; i < num_periods; i++) {
          struct stm32_dma_sg_req *sg = &desc->sg_req[i];

          /* Calculate period address */
          sg->dma_config.dma_periph_data = chan->config.dst_addr;
          sg->dma_config.dma_memory_data = buf_addr + (i * period_len);
          sg->dma_config.dma_size = period_len;

          /* Setup hardware register values */
          sg->dma_config.dma_cfg = chan->config.channel_cfg;
      }

      /* Mark as cyclic */
      dma_async_tx_descriptor_init(&desc->desc, &chan->chan);
      desc->desc.tx_submit = stm32_dma_tx_submit;

      return desc;
  }
```

  5. Real Example: PL330 (drivers/dma/pl330.c)

```c
  /* Hardware instruction format (PL330 uses instruction-based DMA) */
  struct pl330_instr {
      u8 opcode;
      u32 operands[2];
  };

  struct pl330_desc {
      struct dma_async_tx_descriptor tx;
      struct list_head node;

      /* Instruction buffer (PL330 executes instructions) */
      struct pl330_instr *mc;     /* Microcode buffer */
      dma_addr_t mc_dma;          /* DMA address of microcode */
      size_t mc_len;              /* Microcode length */

      struct pl330_xfer {
          dma_addr_t src_addr;
          dma_addr_t dst_addr;
          size_t bytes;
      } *xfers;

      int num_xfers;
      bool cyclic;
  };

  /* Common descriptor allocation */
  static struct pl330_desc *pl330_get_desc(struct pl330_chan *pch)
  {
      struct pl330_desc *desc;

      /* Get from channel's descriptor pool */
      desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
      if (!desc)
          return NULL;

      /* Allocate microcode buffer in DMA-coherent memory */
      desc->mc = dma_alloc_coherent(pch->dmac->ddma.dev,
                                     PL330_MC_BUFFER_SIZE,
                                     &desc->mc_dma,
                                     GFP_NOWAIT);
      if (!desc->mc) {
          kfree(desc);
          return NULL;
      }

      dma_async_tx_descriptor_init(&desc->tx, &pch->chan);
      desc->tx.tx_submit = pl330_tx_submit;

      return desc;
  }
```

  6. Common Infrastructure: DMA Descriptor Pools

  Many drivers use pre-allocated pools to avoid kmalloc in atomic context:

```c
  /* COMMON PATTERN: Descriptor pool management */

  static int dma_descriptor_pool_init(struct dma_chan *chan, int num_descs)
  {
      struct my_dma_chan *my_chan = to_my_chan(chan);
      int i;

      INIT_LIST_HEAD(&my_chan->free_descs);

      for (i = 0; i < num_descs; i++) {
          struct my_dma_desc *desc = kzalloc(sizeof(*desc), GFP_KERNEL);
          if (!desc)
              goto cleanup;

          /* Pre-allocate hardware descriptors too */
          desc->hw = dma_alloc_coherent(chan->device->dev,
                                         sizeof(struct my_hw_desc),
                                         &desc->hw_addr,
                                         GFP_KERNEL);
          if (!desc->hw) {
              kfree(desc);
              goto cleanup;
          }

          dma_async_tx_descriptor_init(&desc->async_tx, chan);
          list_add_tail(&desc->node, &my_chan->free_descs);
      }

      return 0;

  cleanup:
      /* Free all allocated descriptors */
      // ...
  }
```
  7. Common Helpers in DMA Engine Core

```c
  // drivers/dma/dma-mapping.h - Common helpers

  /* Virtual DMA (vdma) - Common descriptor management */
  struct virt_dma_desc {
      struct dma_async_tx_descriptor tx;
      struct list_head node;
      u32 flags;
  };

  struct virt_dma_chan {
      struct dma_chan chan;
      struct list_head desc_issued;
      struct list_head desc_completed;
      spinlock_t lock;
  };

  /* Common helper functions */
  void vdma_desc_get(struct virt_dma_desc *vd);
  void vdma_desc_put(struct virt_dma_desc *vd);
  void vdma_desc_free(struct virt_dma_desc *vd);
```

  /* Used by many drivers: pl08x, mmp_pdma, bcm2835-dma, etc. */

  8. Summary: Common Logic Across All Drivers
```text
  ┌────────────────────────────────┬────────────────────────────────────────┬──────────────────┐
  │         Common Pattern         │                Purpose                 │     Example      │
  ├────────────────────────────────┼────────────────────────────────────────┼──────────────────┤
  │ Embed dma_async_tx_descriptor  │ Generic callback/cookie handling       │ All drivers      │
  ├────────────────────────────────┼────────────────────────────────────────┼──────────────────┤
  │ Descriptor pooling             │ Avoid kmalloc in atomic context        │ dw, pl330, stm32 │
  ├────────────────────────────────┼────────────────────────────────────────┼──────────────────┤
  │ DMA-coherent allocation        │ Hardware descriptors accessible by DMA │ All drivers      │
  ├────────────────────────────────┼────────────────────────────────────────┼──────────────────┤
  │ List-based chaining            │ Link descriptors together              │ All drivers      │
  ├────────────────────────────────┼────────────────────────────────────────┼──────────────────┤
  │ Free list management           │ Reuse descriptors after completion     │ Most drivers     │
  ├────────────────────────────────┼────────────────────────────────────────┼──────────────────┤
  │ dma_async_tx_descriptor_init() │ Initialize generic part                │ All drivers      │
  └────────────────────────────────┴────────────────────────────────────────┴──────────────────┘
```
  9. Typical Cyclic Descriptor Flow (Common Across Drivers)

```c
  /* === THIS PATTERN IS NEARLY IDENTICAL ACROSS DRIVERS === */

  static struct dma_async_tx_descriptor *
  xxx_prep_dma_cyclic(struct dma_chan *chan,
                      dma_addr_t buf_addr,
                      size_t buf_len,
                      size_t period_len,
                      enum dma_transfer_direction dir,
                      unsigned long flags)
  {
      struct xxx_chan *xxx_chan = to_xxx_chan(chan);
      struct xxx_desc *desc;
      int num_periods = buf_len / period_len;
      int i;

      /* 1. Allocate descriptor */
      desc = xxx_alloc_desc(xxx_chan);
      if (!desc)
          return NULL;

      /* 2. Allocate per-period entries */
      desc->periods = kcalloc(num_periods, sizeof(*desc->periods), GFP_NOWAIT);

      /* 3. Build descriptor chain - SAME PATTERN */
      for (i = 0; i < num_periods; i++) {
          desc->periods[i].addr = buf_addr + (i * period_len);
          desc->periods[i].len = period_len;
          /* Setup hardware-specific registers */
      }

      /* 4. Mark as cyclic */
      desc->cyclic = true;
      desc->num_periods = num_periods;

      /* 5. Initialize generic descriptor */
      dma_async_tx_descriptor_init(&desc->async_tx, chan);
      desc->async_tx.tx_submit = xxx_tx_submit;

      return &desc->async_tx;
  }
```
  ---
  Yes! While hardware differs, the descriptor allocation patterns are remarkably similar:
  1. Embed dma_async_tx_descriptor for generic handling
  2. Use descriptor pools to avoid atomic allocations
  3. Allocate hardware descriptors in DMA-coherent memory
  4. Use linked lists for chaining
  5. Return descriptors to free pool after completion

  The DMA Engine Core provides dma_async_tx_descriptor_init() and the virt_dma helpers specifically to share this common logic!
