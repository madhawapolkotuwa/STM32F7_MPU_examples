# Example 01: MPU fault on a forbidden region

### Task:
>> one 256-byte region is set to “no access”. The CPU reads from it, the MPU blocks the read, and a MemManage fault handler prints the fault registers.

### Step1. Create the project (CubeMX)

1. In STM32CubeIDE: File → New → STM32 Project, then use the MCU/MPU Selector to Select the Board. (Ex in My case: `STM32F769NIHx` or `STM32F769I-Discovery`).
2. Name it `MPU_Ex01` and choose C, Executable, STM32Cube.
3. If it asks to initialize peripherals with defaults, answer No.

### Step 2. CubeMX settings
Where | Setting
------|--------
System Core → SYS | Debug: Serial Wire (otherwise you lose the debugger)
System Core → RCC | High Speed Clock (HSE): Crystal/Ceramic Resonator
Clock Configuration | HSE 25 MHz (the board’s crystal, verify), PLL as source, type 216 in HCLK and press Enter. Accept the overdrive prompt.
Connectivity → USART1 | Mode: Asynchronous, 115200 baud, 8 bits, no parity, 1 stop bit. Pins PA9 (TX) and PA10 (RX) go to the ST-LINK virtual COM port (verify on MB1225-F769I-B02).
GPIO | PJ13 as GPIO_Output, User Label LED. In the BSP this is LED1 (verify pin and colour on the schematic).
System Core → CORTEX_M7 | **CPU ICache: Disabled**, **CPU DCache: Disabled**. Leave the MPU control mode at** MPU Disabled**. We configure the MPU in code so it is visible on screen.
NVIC → System interrupts | Leave Memory management fault enabled. This only generates the `MemManage_Handler` stub. It does not enable the fault in hardware (Step 4 does).
Project Manager → Project | Toolchain/IDE: STM32CubeIDE

>> Press Generate Code.

### Step 3. Linker script `STM32F769NIHX_FLASH.ld`

**Why this is needed:** the stack starts at the top of `RAM` and grows downward. If your forbidden region sits inside memory the linker also uses (stack, heap, `.bss`), normal code will fault. So we cut a 4 KB piece off the end of `RAM` and keep it for the test.

In My Case `STM32F76xx`:   
`SRAM1` runs from `0x2002_0000` to `0x2007_BFFF`.

Open the file and find the `MEMORY` block. It will look something like this, but your lines may differ :
```
/* Memories definition */
MEMORY
{
    RAM  (xrw) : ORIGIN = 0x20000000, LENGTH = 512K
    FLASH (rx) : ORIGIN = 0x8000000, LENGTH  = 2048K
}
```
Change it to:
```
/* Memories definition */
MEMORY
{
  RAM       (xrw) : ORIGIN = 0x20000000, LENGTH = 508K
  MPU_TEST  (rw)  : ORIGIN = 0x2007F000, LENGTH = 4K
  FLASH     (rx)  : ORIGIN = 0x8000000,  LENGTH = 2048K
}
```
* `508 KB` is `0x7F000`, so `RAM` now ends at `0x2007_F000`. `MPU_TEST` is the last `4 KB` of `SRAM2`, up to `0x2007_FFFF`.
* Nothing is placed in `MPU_TEST`, so it stays untouched.
* Look for the line `_estack = ORIGIN(RAM) + LENGTH(RAM);` in the file . Because it is computed from RAM, the stack top moves down to `0x2007_F000` automatically.
* Without this change the stack would start at `0x2008_0000` and grow downward right through the region. The first push or function call near the top of the stack would fault, even before the test read.

### Step 4. `main.c`

All code goes between the `USER CODE BEGIN/END` comments, so regeneration will not delete it.

Includes (`USER CODE BEGIN Includes`):
```c
#include <stdio.h>
```
Private defines and functions (`USER CODE BEGIN 0`):
```c
#define FORBIDDEN_ADDR  0x2007F000UL     /* start of MPU_TEST from the linker script */

/* printf over USART1 */
int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
}

static void MPU_Config(void)
{
  MPU_Region_InitTypeDef r = {0};

  HAL_MPU_Disable();

  r.Enable           = MPU_REGION_ENABLE;
  r.Number           = MPU_REGION_NUMBER0;
  r.BaseAddress      = FORBIDDEN_ADDR;               /* multiple of 256 */
  r.Size             = MPU_REGION_SIZE_256B;
  r.SubRegionDisable = 0x00;
  r.TypeExtField     = MPU_TEX_LEVEL1;               /* Normal, non-cacheable */
  r.AccessPermission = MPU_REGION_NO_ACCESS;         /* the point of this example */
  r.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  r.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&r);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);            /* rest of memory uses the default map */
}
```

In `main()`, after `MX_USART1_UART_Init();` (`USER CODE BEGIN 2`):
```c
setvbuf(stdout, NULL, _IONBF, 0);                  /* send printf output immediately */
printf("\r\nExample 01: MPU forbidden region\r\n");

SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;           /* enable MemManage fault, else HardFault */
MPU_Config();
printf("MPU on. Region 0: 0x%08lX, 256 B, no access\r\n", FORBIDDEN_ADDR);

printf("Reading 0x%08lX ...\r\n", FORBIDDEN_ADDR);
volatile uint32_t x = *(volatile uint32_t *)FORBIDDEN_ADDR;   /* this read must fault */
(void)x;

printf("Not reached\r\n");
```
The `while (1)` loop in main can stay empty.

### Step 5. The fault handler in `stm32f7xx_it.c`

Add at the top (`USER CODE BEGIN Includes`):
```c
#include <stdio.h>
extern UART_HandleTypeDef huart1;
```
Inside `MemManage_Handler`, in the first user block (U`SER CODE BEGIN MemoryManagement_IRQn 0`):
```c
uint32_t cfsr = SCB->CFSR;
printf("\r\n*** MemManage fault ***\r\n");
printf("CFSR  = 0x%08lX\r\n", cfsr);
if (cfsr & (1UL << 0)) printf("IACCVIOL: instruction accessviolation\r\n");
if (cfsr & (1UL << 1)) printf("DACCVIOL: data accessviolation\r\n");
if (cfsr & (1UL << 7)) printf("MMFAR = 0x%08lX\r\n",SCB->MMFAR);   /* valid only if MMARVALID */
HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_13, GPIO_PIN_SET);
```

### Step 6. Run it
1. Build (hammer icon) and start a Debug session.   
2. Open a serial terminal on the ST-LINK COM port: 115200, 8N1.     
3. Press Run.

## Result Analysis

### Expected terminal output:

```
Example 01: MPU forbidden region
MPU on. Region 0: 0x2007F000, 256 B, no access
Reading 0x2007F000 ...

*** MemManage fault ***
CFSR  = 0x00000082
DACCVIOL: data access violation
MMFAR = 0x2007F000
```

### What we showed

The MPU can stop the CPU from touching memory it was not given. We marked a small piece of perfectly good RAM as “no access”, read from it, and the hardware blocked the read and raised a fault. The read never completed, and nothing was silently corrupted.

### Reading the result line by line
1. `MPU on. Region 0: 0x2007F000, 256 B, no access` is our setup. One MPU region, 256 bytes long, starting at `0x2007F000`, with access permission set to “**no access**”. Every other address keeps the default memory map. 
2. `Reading 0x2007F000 ...` is the last line printed before the access. The next line of code is a plain C read from that address. The memory exists and nothing is wrong with it. The MPU is the only thing stopping us.
3. `*** MemManage fault ***` means the MPU checked the read, found it forbidden, and the CPU jumped to `MemManage_Handler` instead of finishing the read. “**Not reached**” never prints, which is the proof the program was stopped.
4. `CFSR = 0x00000082` is the Configurable Fault Status Register. It tells you why the fault happened. `0x82` is two bits set:
    * **Bit 1, DACCVIOL**: a data access violation. The CPU tried to read or write data where it was not allowed. (Bit 0, IACCVIOL, would mean an instruction fetch violation, which is Example 02.)
    * **Bit 7, MMARVALID**: the address register holds a valid value.
5. `MMFAR = 0x2007F000` is the MemManage Fault Address Register. It holds the exact address that was accessed, and it matches the one we protected. The MPU tells you what happened and where.

### Key takeaways

* The MPU checks every access, before it reaches memory.
* A violation gives you a precise fault with a reason (CFSR) and an address (MMFAR), instead of a random crash later.
* A fault in a handler like this is how you find bugs such as a stray pointer or a stack overflow into a protected area.
* The fault only reached our handler because we enabled it with `MEMFAULTENA`. Without it, the same access escalates to a HardFault, which is the mistake to show in Step 7 of the example.

