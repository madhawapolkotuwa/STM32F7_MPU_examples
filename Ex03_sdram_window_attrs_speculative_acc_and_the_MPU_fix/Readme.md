## Example 03: SDRAM Window Attributes, Speculative Access and the MPU Fix

First, what this example can and cannot show. Speculative reads and prefetches are decisions the core makes on its own. I can’t give you a loop that triggers a speculative bus fault on demand, and a fault may or may not appear on your board. So this example proves the part that is deterministic: what each MPU setting allows the core to do with the SDRAM window. You run the same test three times and compare.

Run | MODE | MPU setting for the FMC SDRAM window | What it means
----|------|--------------------------------------|--------------
Default | 0 | MPU off | The default map treats 0xC000_0000 as Device + XN (verify with the unaligned test below)
Setup A (risky) | 1 | One 512 MB region: Normal, cacheable, executable | The core may cache, prefetch and speculate anywhere in 512 MB, including memory that does not exist
Setup B (fix) | 2 | Region 0: whole 512 MB Device + XN. Region 1: only the populated 16 MB Normal, cacheable, XN | Only the real SDRAM can be cached. Everything else stays protected

STM32F769: `0xC000_0000` to `0xDFFF_FFFF` is F**MC SDRAM bank 1** and **bank 2**, **512 MB** in total. This `Discovery` board only uses part of **bank 1**.

#### Step 1. Project and CubeMX settings

Use a new project MPU_Ex03 with the Example 01 settings (SYS Serial Wire, HSE, 216 MHz, USART1 on PA9 and PA10, PJ13 as LED, CORTEX_M7 with ICache, DCache and MPU Disabled).

Add the FMC exactly as in your finished SDRAM tutorial:

* Connectivity → FMC → SDRAM 1: ![check here](https://github.com/madhawapolkotuwa/stm32f7_sdram_test/tree/main) how to configure **SDRAM**.
* From that project, also copy the function with the JEDEC power-up sequence. `SDRAM_Init_Sequence()`. 

#### Step 2. Linker script

No change. SDRAM is not used by the linker script. Keep your original MEMORY block.

#### Step 3. main.c

Includes (USER CODE BEGIN Includes):
```c
#include <stdio.h>
```

Private code (USER CODE BEGIN 0):
```c
#define MODE        0                 /* 0 = default, 1 = Setup A, 2 = Setup B */
#define SDRAM_BASE  0xC0000000UL
#define TEST_WORDS  1024              /* 4 KB, fits in the 16 KB D-cache */

int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
}

static void dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55;              /* unlock, needed on some Cortex-M7 revisions (verify) */
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef r = {0};
  HAL_MPU_Disable();

  /* Region 0: the whole FMC SDRAM window, bank 1 and bank 2 (512 MB) */
  r.Enable           = MPU_REGION_ENABLE;
  r.Number           = MPU_REGION_NUMBER0;
  r.BaseAddress      = 0xC0000000UL;
  r.Size             = MPU_REGION_SIZE_512MB;
  r.SubRegionDisable = 0x00;
  r.AccessPermission = MPU_REGION_FULL_ACCESS;
#if MODE == 1
  /* Setup A: Normal, WB-RWA, executable (the risky setup) */
  r.TypeExtField = MPU_TEX_LEVEL1;
  r.IsShareable  = MPU_ACCESS_NOT_SHAREABLE;
  r.IsCacheable  = MPU_ACCESS_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_BUFFERABLE;
  r.DisableExec  = MPU_INSTRUCTION_ACCESS_ENABLE;
#else
  /* Setup B background: Device + XN */
  r.TypeExtField = MPU_TEX_LEVEL0;
  r.IsShareable  = MPU_ACCESS_SHAREABLE;
  r.IsCacheable  = MPU_ACCESS_NOT_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_BUFFERABLE;
  r.DisableExec  = MPU_INSTRUCTION_ACCESS_DISABLE;
#endif
  HAL_MPU_ConfigRegion(&r);

#if MODE == 2
  /* Region 1: only the populated SDRAM, Normal, WB-RWA, XN. Higher number wins. */
  r.Number       = MPU_REGION_NUMBER1;
  r.BaseAddress  = SDRAM_BASE;
  r.Size         = MPU_REGION_SIZE_16MB;      /* verify your SDRAM size */
  r.TypeExtField = MPU_TEX_LEVEL1;
  r.IsShareable  = MPU_ACCESS_NOT_SHAREABLE;
  r.IsCacheable  = MPU_ACCESS_CACHEABLE;
  r.IsBufferable = MPU_ACCESS_BUFFERABLE;
  r.DisableExec  = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&r);
#endif

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static uint32_t read_pass(void)
{
  volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
  uint32_t sum = 0;
  uint32_t t0 = DWT->CYCCNT;
  for (int i = 0; i < TEST_WORDS; i++) sum += p[i];
  uint32_t dt = DWT->CYCCNT - t0;
  (void)sum;
  return dt;
}

/* Shared by all fault handlers (called from stm32f7xx_it.c) */
void Fault_Report(const char *name)
{
  uint32_t cfsr = SCB->CFSR;
  printf("\r\n*** %s ***\r\nCFSR = 0x%08lX\r\n", name, cfsr);
  if (cfsr & (1UL << 24)) printf("UNALIGNED (UsageFault)\r\n");
  if (cfsr & (1UL << 17)) printf("INVSTATE (UsageFault)\r\n");
  if (cfsr & (1UL << 0))  printf("IACCVIOL (MemManage)\r\n");
  if (cfsr & (1UL << 1))  printf("DACCVIOL (MemManage)\r\n");
  if (cfsr & (1UL << 7))  printf("MMFAR = 0x%08lX\r\n", SCB->MMFAR);
  if (cfsr & (1UL << 8))  printf("IBUSERR (BusFault)\r\n");
  if (cfsr & (1UL << 9))  printf("PRECISERR (BusFault)\r\n");
  if (cfsr & (1UL << 10)) printf("IMPRECISERR (BusFault)\r\n");
  if (cfsr & (1UL << 15)) printf("BFAR = 0x%08lX\r\n", SCB->BFAR);
  HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_13, GPIO_PIN_SET);
}
```

In main(), after MX_FMC_Init(); and MX_USART1_UART_Init(); (USER CODE BEGIN 2):

```c
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("\r\nExample 03, MODE %d\r\n", MODE);

  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;

  SDRAM_Init_Sequence(&hsdram1);      /* function from the SDRAM project */
  dwt_init();

#if MODE != 0
  MPU_Config();                         /* MPU first ... */
#endif
  SCB_EnableICache();                   /* ... then caches */
  SCB_EnableDCache();

  /* Fill the test area, then empty the cache so the first read is cold */
  volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
  for (int i = 0; i < TEST_WORDS; i++) p[i] = i;
  SCB_CleanInvalidateDCache();

  uint32_t c1 = read_pass();            /* cold */
  uint32_t c2 = read_pass();            /* warm */
  printf("Read pass 1: %lu cycles\r\nRead pass 2: %lu cycles\r\n", c1, c2);

  printf("Unaligned 32-bit read at 0x%08lX ...\r\n", SDRAM_BASE + 1);
  volatile uint32_t u = *(volatile uint32_t *)(SDRAM_BASE + 1);
  printf("Unaligned read OK, value 0x%08lX\r\n", u);
```

#### Step 4. Fault handlers in stm32f7xx_it.c

At the top (USER CODE BEGIN Includes):
```c
void Fault_Report(const char *name);
```

Then in the first user block of each handler:
```c
/* HardFault_Handler   (USER CODE BEGIN HardFault_IRQn 0)       */  Fault_Report("HardFault");
/* MemManage_Handler   (USER CODE BEGIN MemoryManagement_IRQn 0) */  Fault_Report("MemManage");
/* BusFault_Handler    (USER CODE BEGIN BusFault_IRQn 0)         */  Fault_Report("BusFault");
/* UsageFault_Handler  (USER CODE BEGIN UsageFault_IRQn 0)       */  Fault_Report("UsageFault");
```

#### Step 5. Run it three times

Build and run with MODE 0, then change the define to 1 and run again, then 2. Use the serial terminal at 115200, 8N1.

What to expect. Cycle counts depend on your clocks and SDRAM timing, so compare the runs with each other, not with fixed numbers (verify on hardware).

Observation | MODE 0 (default) | MODE 1 (Setup A) | MODE 2 (Setup B)
------------|------------------|------------------|------------------
Read pass 1 vs read pass 2 | Both large and about equal | Pass 1 large, pass 2 much smaller | Same as MODE 1
Unaligned read at base + 1 | Expected to fault (see below) | Works and prints the value | Works and prints the value

### What we are testing

The FMC SDRAM window (0xC000_0000 to 0xDFFF_FFFF, banks 1 and 2) is 512 MB of address space, but the board has one 16 MB chip (ISSI IS42S32400F, 128 Mbit, 4M x 32) behind bank 1. The core can cache, prefetch and speculate in any address range the MPU marks as Normal and cacheable, whether or not memory exists there.

The core decides on its own when to speculate, so we cannot make it do so on demand. What we can show is what each MPU setting allows. The same test runs three times:

| Run | MPU setting for the SDRAM window | Purpose |
|---|---|---|
| MODE 0 | MPU off, default map | See the default attributes |
| MODE 1 (Setup A) | One 512 MB region: Normal, write-back, cacheable, executable | The risky, copy-and-paste setup |
| MODE 2 (Setup B) | Region 0: all 512 MB Device + XN. Region 1: the populated 16 MB Normal, cacheable, XN | The fix |

The test reads a 4 KB block in SDRAM twice (1024 words) and counts cycles with the DWT counter. The cache is emptied before the first pass. Then it does an unaligned 32-bit read at 0xC000_0001.

### Results

| | MODE 0 | MODE 1 | MODE 2 |
|---|---|---|---|
| Read pass 1 (cycles) | 23990 | 15227 | 15224 |
| Read pass 2 (cycles) | 23903 | 13838 | 13838 |
| Unaligned read at base + 1 | UsageFault, UNALIGNED (`CFSR = 0x01000000`) | Works, value `0x01000000` | Works, value `0x01000000` |


### MODE 0: the default map

```
Read pass 1: 23990 cycles
Read pass 2: 23903 cycles
Unaligned 32-bit read at 0xC0000001 ...
*** UsageFault ***
CFSR = 0x01000000
UNALIGNED (UsageFault)
```

* Both passes cost about the same, about 23.4 cycles per word. The caches were enabled, yet the second pass is not faster. The data was never cached, so every read was a separate trip to the SDRAM.
* The unaligned read raised a **UsageFault**. `CFSR = 0x01000000` is bit 24, **UNALIGNED**. The Cortex-M7 does not allow unaligned access to Device or Strongly-ordered memory, and the fault comes from the memory type itself, not from a bus error.
* The two results agree: by default the window is **Device** memory. It is not cached, there are no speculative reads, and there is no unaligned access. That is why SDRAM works at 0xC000_0000 with no MPU setup, and it is the safe default.


### MODE 1 and MODE 2: cacheable SDRAM

```
Read pass 1: 15227 cycles          Read pass 1: 15224 cycles
Read pass 2: 13838 cycles          Read pass 2: 13838 cycles
Unaligned read OK, value 0x01000000
```


* **Both modes behave the same**, as expected. For the 16 MB we actually use, Setup A and Setup B give the same memory attributes. The difference between them is in the other 496 MB, which this test does not touch.
* **The speed-up is real but looks small.** Pass 2 is about 13.5 cycles per word. A data cache hit takes only a cycle or so, so most of that time is the loop itself (loading and storing the loop counter and sum on the stack, which is typical of an unoptimised debug build). We can estimate the memory cost by subtracting that baseline:
  * Uncached (MODE 0 pass 2 minus MODE 1 pass 2): 23903 - 13838 = 10065 cycles, about **10 cycles per word** of SDRAM latency.
  * Cold cache fill (MODE 1 pass 1 minus pass 2): 15227 - 13838 = 1389 cycles for the 128 cache lines (4096 B / 32 B), about **11 cycles per line**.
  * Cached hits cost almost nothing, which is why pass 2 settles at the loop's own cost.
* To make the cache effect more visible, rebuild with optimisation (`-O2` or a Release build) and run again. Pass 2 should drop sharply. The exact numbers depend on your clocks and SDRAM timing *(verify on your setup)*.
* **The unaligned read now works.** The region is Normal memory, so unaligned access is allowed. The value `0x01000000` is correct. The buffer holds the word index at each word, so the bytes at offsets 1 to 3 are 0 and the byte at offset 4 is 1, and the little-endian result is `0x01000000`.
* The one-line MPU region changed the type of the **whole 512 MB window**, not only the chip.

### Why Setup A is risky and Setup B is the fix

* In Setup A, the core is allowed to do cache linefills, speculative reads and instruction prefetches in all 512 MB. Only 16 MB of it is real SDRAM. A speculative access to an address with no memory behind it, or before the FMC is configured, can give a bus error or stall.
* The test does not make that happen, and on your board it may never happen. A clean run in Setup A does not prove the setup is safe, it only shows that the core did not speculate into the gap during this test.
* In Setup B the background region keeps the entire window as **Device + XN**, so nothing outside the real chip can be cached, prefetched or executed. Region 1, which has the higher number and therefore wins where they overlap, makes only the populated 16 MB Normal and cacheable. XN on the 16 MB also means that speculative instruction fetches cannot go there, since it holds data only.


### Release build results (optimised)

The debug build hides most of the cache effect, because the unoptimised loop spends about 13 cycles per word on its own work (loop counter and sum kept on the stack). Rebuilding with optimisation removes that overhead:

| | MODE 0 (default, Device) | MODE 1 (Setup A) | MODE 2 (Setup B) |
|---|---|---|---|
| Read pass 1 (cycles) | 11614 | 4173 | 4161 |
| Read pass 2 (cycles) | 11632 | 1543 | 1543 |
| Unaligned read at base + 1 | UsageFault, UNALIGNED (`CFSR = 0x01000000`) | Works, `0x01000000` | Works, `0x01000000` |

What the numbers tell us:

* **MODE 0 costs about 11.3 cycles per word** (11614 / 1024) on both passes. The two passes are equal because the window is Device memory and is never cached. Every word is a separate access to the SDRAM, so this figure is the raw SDRAM read latency seen by the CPU in your configuration.
* **MODE 1 and 2, pass 2, cost about 1.5 cycles per word** (1543 / 1024). The 4 KB block now sits in the D-cache, so the loop is limited by the CPU, not by the SDRAM.
* **MODE 1 and 2, pass 1, cost about 4 cycles per word** (4173 / 1024). This pass includes filling the cache. The fill adds about 2630 cycles (4173 - 1543) for 128 cache lines (4096 B / 32 B), roughly **20 cycles per 32-byte line**.
* **Why the cached cold pass beats the uncached one.** Even the first pass is 2.8 times faster than MODE 0 (11614 / 4173). A cache line fill reads 32 bytes (8 words) in one burst, which costs about 20 cycles, or 2.5 cycles per word. Eight separate single-word reads cost about 90 cycles.
* **Cached against uncached overall:** the second pass is about **7.5 times faster** than MODE 0 (11632 / 1543).
* **MODE 1 and 2 give the same result.** The 12-cycle difference in pass 1 (4173 vs 4161) is measurement noise. For the 16 MB we use, both setups give the same memory attributes, so the fix costs nothing in speed.
* **The unaligned read result still identifies the memory type.** It faults in MODE 0 (Device) and works in MODE 1 and 2 (Normal).

The debug build showed a fill cost of only about 11 cycles per line, which is about half. That figure was misleading, because the slow loop overlapped with the cache fill and hid part of the latency. Use the release numbers when you talk about memory speed.

Takeaway for the video: "The cache only pays off when the loop is fast enough to notice. In an unoptimised build the CPU is the bottleneck. With optimisation on, the SDRAM is the bottleneck, and the cache removes it."

These cycle counts depend on your clocks and FMC timing. They describe this setup, not every STM32F769 board.

### Summary

| Observation | What it shows |
|---|---|
| MODE 0 passes equal, no speed-up | Default 0xC000_0000 window is not cached |
| MODE 0 unaligned read faults (UNALIGNED) | Default window is Device memory |
| MODE 1 and 2 pass 2 faster than MODE 0 | Normal + cacheable lets the D-cache work |
| MODE 1 and 2 give the same numbers | The fix costs nothing in performance |
| Unaligned read works in MODE 1 and 2 | The region is now Normal memory |


### Takeaways

* Never put one big Normal region over an external memory window. Cover the whole window with Device + XN, then add a smaller Normal region over the memory that really exists.
* Region numbers matter. The higher number wins where regions overlap.
* The region size must be a power of two and the base must be aligned to it. 16 MB at 0xC000_0000 satisfies both.
* Check the memory type with a test that cannot be argued with. Here, the unaligned read tells you directly whether a region is Device or Normal.
* Configure the MPU first, then enable the caches.










