## Example 04: DMA Writes, CPU Reads Stale Data (Fix: Invalidate)

DMA2 copies a buffer into RAM, but the CPU still reads the old values from the D-cache. Invalidating the cache lines fixes it.

Sequence:

1. The CPU writes zeros to dst, cleans them to RAM, then reads them, so dst is now in the D-cache.
2. DMA2 copies src (all 0xAAAAAAAA) into dst. This updates RAM only.
3. The CPU reads dst. It gets the old zeros (stale).
4. The CPU invalidates dst and reads again. It now gets 0xAAAAAAAA.

#### Step 1. Project and CubeMX settings

Create `MPU_Ex04` with the same base settings as Example 01.

Where | Setting
------|--------
SYS | Debug: Serial Wire
RCC | HSE: Crystal/Ceramic Resonator; HCLK 216 MHz
USART1 | Asynchronous, 115200 8N1, PA9 and PA10 (verify)
CORTEX_M7 | ICache Disabled, DCache Disabled, MPU Disabled (we enable the caches in code)
Project Manager | Toolchain: STM32CubeIDE

**DMA2 memory-to-memory**:

1. System Core → DMA → MemToMem tab → Add.
2. Select DMA2 Stream 0 and configure it like this:

Field | Value
------|-------
Direction | Memory To Memory
Priority | Low
Mode | Normal
Increment Address | Source and Destination both checked
Data Width | Word and Word
Use FIFO | Checked, threshold Full (memory-to-memory needs the FIFO)
Burst Size | Single and Single

Generate code. CubeMX creates `hdma_memtomem_dma2_stream0` and `MX_DMA_Init()`.

>> We use polling, so no DMA interrupt is needed.

#### Step 2. Linker script `STM32F769NIHX_FLASH.ld`

The default script puts `.data`, `.bss` and the stack at the start of RAM, which is DTCM (0x2000_0000). DTCM is not cached, so buffers there would never show this bug. The test buffers must go into cached SRAM. Reserve 4 KB at the end of RAM, as in Examples 01 and 02:

**Edit 1: MEMORY block**
```
MEMORY
{
  RAM       (xrw) : ORIGIN = 0x20000000, LENGTH = 508K
  DMA_BUF   (rw)  : ORIGIN = 0x2007F000, LENGTH = 4K
  FLASH     (rx)  : ORIGIN = 0x8000000,  LENGTH = 2048K
}
```

**Edit 2: new output section.** Add this inside SECTIONS { ... }, right after the closing } of the `.bss` block:     
```
  .dma_buf (NOLOAD) :
  {
    . = ALIGN(32);
    *(.dma_buf)
    *(.dma_buf*)
    . = ALIGN(32);
  } >DMA_BUF
```
>> `NOLOAD` means nothing is copied from flash. The buffers start uninitialised, and the program fills them. The `ALIGN(32)` keeps the section on a cache-line boundary.

#### Step 3. main.c

Includes (USER CODE BEGIN Includes):
```c
#include <stdio.h>
```

Private code (USER CODE BEGIN 0):
```c
#define BUF_WORDS  64                       /* 64 words = 256 bytes = 8 cache lines */

/* Both buffers: in the cached SRAM section, 32-byte aligned, size a multiple of 32 */
__attribute__((section(".dma_buf"), aligned(32))) volatile uint32_t src[BUF_WORDS];
__attribute__((section(".dma_buf"), aligned(32))) volatile uint32_t dst[BUF_WORDS];

int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
}

static uint32_t count_match(uint32_t value)         /* how many dst words equal 'value' */
{
  uint32_t n = 0;
  for (int i = 0; i < BUF_WORDS; i++) if (dst[i] == value) n++;
  return n;
}

static void dma_copy(void)                          /* DMA2: src -> dst */
{
  /* the length is in DATA ITEMS: the DMA is set to Word width, so 64 words */
  HAL_DMA_Start(&hdma_memtomem_dma2_stream0, (uint32_t)src, (uint32_t)dst, BUF_WORDS);
  if (HAL_DMA_PollForTransfer(&hdma_memtomem_dma2_stream0, HAL_DMA_FULL_TRANSFER, 100) != HAL_OK)
    printf("DMA error\r\n");
}
```

In `main()`, after `MX_DMA_Init();` and `MX_USART1_UART_Init();` (USER CODE BEGIN 2):
```c
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("\r\nExample 04: DMA write, CPU read (D-cache on)\r\n");
  printf("src @ 0x%08lX, dst @ 0x%08lX\r\n", (uint32_t)src, (uint32_t)dst);

  SCB_EnableICache();
  SCB_EnableDCache();

  /* 1. Source data: written by the CPU, then pushed to RAM so the DMA can read it */
  for (int i = 0; i < BUF_WORDS; i++) src[i] = 0xAAAAAAAA;
  SCB_CleanDCache_by_Addr((uint32_t *)src, sizeof(src));

  /* 2. Destination: zeros in RAM, and the lines are then loaded into the D-cache */
  for (int i = 0; i < BUF_WORDS; i++) dst[i] = 0;
  SCB_CleanDCache_by_Addr((uint32_t *)dst, sizeof(dst));
  printf("Before DMA : dst words = 0x00000000: %lu / %d\r\n", count_match(0), BUF_WORDS);
                                                    /* this read pass fills the cache lines */
  /* 3. DMA writes the new data into RAM, behind the cache */
  dma_copy();

  /* 4. CPU reads dst: cache hit, old data */
  printf("After DMA, NO invalidate: dst[0] = 0x%08lX, words = 0xAAAAAAAA: %lu / %d\r\n",
         dst[0], count_match(0xAAAAAAAA), BUF_WORDS);

  /* 5. The fix: drop the cached lines, so the next read goes to RAM */
  SCB_InvalidateDCache_by_Addr((uint32_t *)dst, sizeof(dst));
  printf("After invalidate        : dst[0] = 0x%08lX, words = 0xAAAAAAAA: %lu / %d\r\n",
         dst[0], count_match(0xAAAAAAAA), BUF_WORDS);
```

No changes are needed in the `while (1)` loop or in `stm32f7xx_it.c`.

#### Step 4. Run it

#### Step 5
A. Is it really the cache? Comment out `SCB_EnableDCache();` and run again. Every read is correct without any invalidate. This shows the bug only exists when the D-cache is on.

B. Invalidating too early. Move the invalidate to before the DMA and add one read in between:
```c
  SCB_InvalidateDCache_by_Addr((uint32_t *)dst, sizeof(dst));   /* too early */
  (void)count_match(0);              /* any read here (even a debug printf) reloads the old lines */
  dma_copy();
  /* dst now reads stale again */
```

------------------------------

### What we are testing

This is the first coherency problem from the app note (Figure 3-1). A DMA controller writes into a RAM buffer, and the CPU then reads that buffer through the D-cache. The DMA writes straight to RAM and the CPU reads from the cache, so the CPU can see old data.

Setup:

* Two buffers, `src` and `dst`, 64 words (256 bytes, 8 cache lines) each. They are 32-byte aligned and the size is a multiple of 32, as the app note requires for maintenance operations.
* They sit in a dedicated linker section (`.dma_buf`) at the end of SRAM2. The printed addresses, `0x2007F000` and `0x2007F100`, confirm they are in cached SRAM and not in DTCM, which is never cached.
* The region uses the default SRAM attribute, which is write-back with read and write allocate (WB-RWA). No MPU region was configured for this example.
* DMA2 stream 0 is configured memory-to-memory, word width, and copies `src` (all `0xAAAAAAAA`) into `dst` (all zeros).

Sequence in the program:

1. The CPU fills `src` with `0xAAAAAAAA` and **cleans** it, so the DMA reads the real data from RAM.
2. The CPU writes zeros to `dst`, cleans them to RAM, and reads them back. That read pass loads the `dst` lines into the D-cache (read allocate).
3. DMA2 copies `src` to `dst`. This changes RAM only.
4. The CPU reads `dst`.
5. The CPU invalidates `dst` and reads it again.

### Results

```
Example 04: DMA write, CPU read (D-cache on)
src @ 0x2007F000, dst @ 0x2007F100
Before DMA : dst words = 0x00000000: 64 / 64
After DMA, NO invalidate: dst[0] = 0x00000000, words = 0xAAAAAAAA: 0 / 64
After invalidate : dst[0] = 0xAAAAAAAA, words = 0xAAAAAAAA: 64 / 64
```

| Line | What the CPU saw | What it means |
|---|---|---|
| Before DMA | 64 of 64 words are zero | `dst` holds zeros. This read pass also loaded the lines into the D-cache. |
| After DMA, no invalidate | `dst[0] = 0x00000000`, 0 of 64 words are `0xAAAAAAAA` | The DMA wrote the new data into RAM, but the CPU read the old zeros from its cache. **All 64 words are stale.** |
| After invalidate | `dst[0] = 0xAAAAAAAA`, 64 of 64 words match | The invalidate dropped the cached lines, so the next read missed, went to RAM, and fetched the DMA's data. |

### Why the stale data appears

* The DMA is a bus master that goes **around** the cache. It writes to RAM and nobody tells the D-cache.
* The `dst` lines were already in the cache from the earlier read pass (read allocate), so the CPU's next read is a **cache hit**. The cache happily returns what it holds, which is the old data. It never looks at RAM.
* The Cortex-M7 data cache has no hardware that watches other masters, so software has to resolve this. The fix is `SCB_InvalidateDCache_by_Addr()`.
* RAM was correct the whole time. The debugger's Memory view reads RAM directly, so it would show `0xAAAAAAAA` while the program was still reading zeros. The data is not lost. The CPU is reading a stale copy.

### Why the first clean matters

The CPU wrote zeros to `dst` before the DMA ran. With write-back, those writes would stay in the cache as **dirty** lines. A dirty line gets written back to RAM when it is evicted, and that could overwrite the DMA's fresh data later. Cleaning `dst` before starting the DMA removes that risk, so the only problem left is the stale read. For a receive buffer the CPU has touched before, the safe rule is: **clean before the DMA, invalidate after the DMA.**

### Step 56A: the same test with the D-cache off

```
Example 04: DMA write, CPU read (D-cache off)
Before DMA : dst words = 0x00000000: 64 / 64
After DMA, NO invalidate: dst[0] = 0xAAAAAAAA, words = 0xAAAAAAAA: 64 / 64
After invalidate : dst[0] = 0xAAAAAAAA, words = 0xAAAAAAAA: 64 / 64
```

* The new data is visible immediately, with no invalidate. Every CPU read goes to RAM, so there is nothing stale to read.
* This proves the problem comes from the D-cache and not from the DMA or the buffers.
* It is also the quickest debugging test: if a DMA bug disappears when the D-cache is disabled, you have a coherency problem.
* The price is performance, because the whole D-cache is turned off. The rest of this series covers better fixes.

### Step 5B: invalidating too early

When the invalidate was moved to **before** the DMA, with a CPU read in between, the output returned to `0 / 64` (stale).

* The invalidate dropped the lines, but the read before the DMA loaded the old zeros into the cache again (read allocate).
* The DMA then changed RAM, and the CPU hit the freshly reloaded old lines.
* Rule: **invalidate after the DMA has finished and before the first CPU read of the buffer.** Any CPU access in between, including a `printf` of a buffer value in the debugger or a speculative prefetch of the neighbouring line, can bring the old data back.

### Summary

| Case | Result | Lesson |
|---|---|---|
| D-cache on, no invalidate | 0 / 64 correct, stale zeros | The DMA wrote RAM, the CPU read the cache |
| D-cache on, invalidate after DMA | 64 / 64 correct | Invalidate forces the next read to RAM |
| D-cache off | 64 / 64 correct, no invalidate needed | The bug exists only with the cache |
| Invalidate before DMA, with a read between | 0 / 64 correct, stale zeros | Timing matters: invalidate after the DMA |

### Takeaways

* A DMA write into a cacheable buffer needs `SCB_InvalidateDCache_by_Addr()` before the CPU reads the result. Call it from the DMA-complete handler or right after the transfer wait.
* The buffer address must be 32-byte aligned and its size a multiple of 32. Otherwise the invalidate can destroy neighbouring variables (Example 09).
* Clean a receive buffer before the DMA starts if the CPU wrote to it earlier, so a later eviction cannot overwrite the DMA's data.
* Never use the whole-cache `SCB_InvalidateDCache()` for this. It discards every dirty line in the cache, including variables and the stack.
* DTCM is not cached. Buffers placed there never show this problem, which is why the linker script moved the buffers to SRAM2.