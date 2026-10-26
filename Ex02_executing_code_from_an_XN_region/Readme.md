# Example 02: executing code from an XN region

### Task
>> A two-instruction function is copied into RAM and called. With the region executable it runs and returns 42. Then the same region is set to Execute Never (XN) and the same call faults.

## Step 1. Project

Use a new project `MPU_Ex02` with the same CubeMX settings as **Example 01**, Step 2. A quick checklist:

Where | Setting
------|--------
SYS | Debug: Serial Wire
RCC | HSE: Crystal/Ceramic Resonator; HCLK 216 MHz
USART1 | Asynchronous, 115200 8N1, PA9 and PA10 
GPIO | PJ13 as GPIO_Output, label LED 
CORTEX_M7 | ICache Disabled, DCache Disabled, MPU Disabled
NVIC | Leave Memory management fault and Usage fault enabled (this only generates the handler stubs)
Project Manager | Toolchain: STM32CubeIDE

Caches stay off here so the example only shows XN. 
With caches on, you would also have to clean the D-cache and invalidate the I-cache after copying code. 
That belongs in a later example.

## Step 2. Linker script `STM32F769NIHX_FLASH.ld`

The code needs a RAM spot nothing else uses, so reserve the last 4 KB of SRAM2, the same as Example 01:
```
/* Memories definition */
MEMORY
{
  RAM       (xrw) : ORIGIN = 0x20000000, LENGTH = 508K
  MPU_TEST  (rwx) : ORIGIN = 0x2007F000, LENGTH = 4K
  FLASH     (rx)  : ORIGIN = 0x8000000,  LENGTH = 2048K
}
```
* `RAM` now ends at `0x2007_F000`, so `_estack` moves down automatically ( `_estack = ORIGIN(RAM) + LENGTH(RAM);` ).
* Nothing is placed in `MPU_TEST`. We copy code there at run time.
* If you edit the Example 01 project instead of making a new one, you already have this change.

## Step 3. main.c

Includes (`USER CODE BEGIN Includes`):

```c
#include <stdio.h>
#include <string.h>
```

Private code (`USER CODE BEGIN 0`):

```c
#define CODE_ADDR  0x2007F000UL          /* start of MPU_TEST from the linker script */

/* Two Thumb instructions: movs r0, #42 ; bx lr */
static const uint16_t tiny_func[] = { 0x202A, 0x4770 };

int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
}

/* xn = 0: executable, xn = 1: Execute Never */
static void MPU_Config(uint8_t xn)
{
  MPU_Region_InitTypeDef r = {0};

  HAL_MPU_Disable();

  r.Enable           = MPU_REGION_ENABLE;
  r.Number           = MPU_REGION_NUMBER0;
  r.BaseAddress      = CODE_ADDR;                      /* multiple of 256 */
  r.Size             = MPU_REGION_SIZE_256B;
  r.SubRegionDisable = 0x00;
  r.TypeExtField     = MPU_TEX_LEVEL1;                 /* Normal, non-cacheable */
  r.AccessPermission = MPU_REGION_FULL_ACCESS;         /* data access is allowed */
  r.DisableExec      = xn ? MPU_INSTRUCTION_ACCESS_DISABLE
                          : MPU_INSTRUCTION_ACCESS_ENABLE;
  r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  r.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&r);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void run_ram_func(void)
{
  uint32_t (*fn)(void) = (uint32_t (*)(void))(CODE_ADDR | 1U);   /* bit 0 = Thumb */
  printf("Calling function at 0x%08lX ...\r\n", CODE_ADDR);
  uint32_t v = fn();
  printf("Returned %lu\r\n", v);
}
```

In `main()`, after `MX_USART1_UART_Init();` (`USER CODE BEGIN 2`):
```c
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("\r\nExample 02: Execute Never (XN)\r\n");

  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;             /* enable MemManage fault */

  memcpy((void *)CODE_ADDR, tiny_func, sizeof tiny_func);   /* copy code into RAM */
  __DSB();
  __ISB();

  printf("Step 1: XN off\r\n");
  MPU_Config(0);
  run_ram_func();                                      /* runs, returns 42 */

  printf("Step 2: XN on\r\n");
  MPU_Config(1);
  run_ram_func();                                      /* must fault */

  printf("Not reached\r\n");
```

## Step 4. Fault handler in `stm32f7xx_it.c`

At the top (`USER CODE BEGIN Includes`):
```c
#include <stdio.h>
extern UART_HandleTypeDef huart1;
```
Inside `MemManage_Handler`, first user block (`USER CODE BEGIN MemoryManagement_IRQn 0`):
```c
  uint32_t cfsr = SCB->CFSR;
  printf("\r\n*** MemManage fault ***\r\n");
  printf("CFSR  = 0x%08lX\r\n", cfsr);
  if (cfsr & (1UL << 0)) printf("IACCVIOL: instruction access violation\r\n");
  if (cfsr & (1UL << 1)) printf("DACCVIOL: data access violation\r\n");
  if (cfsr & (1UL << 7)) printf("MMFAR = 0x%08lX\r\n", SCB->MMFAR);
  HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_13, GPIO_PIN_SET);   /* or LED_GPIO_Port / LED_Pin */
```

## Step 5. Run it

Build, start a Debug session, open the serial terminal (115200, 8N1) and press Run.

Expected output:
```
Example 02: Execute Never (XN)
Step 1: XN off
Calling function at 0x2007F000 ...
Returned 42
Step 2: XN on
Calling function at 0x2007F000 ...

*** MemManage fault ***
CFSR  = 0x00000001
IACCVIOL: instruction access violation
```

## Step 6: Forget the Thumb bit

In `run_ram_func()` drop the Thumb bit:
```c
uint32_t (*fn)(void) = (uint32_t (*)(void))(CODE_ADDR);   /* BUG on purpose: no Thumb bit */
```

In `main()`, enable both faults (`replace the one SHCSR line`):
```c
SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
```

In `stm32f7xx_it.c`, `UsageFault_Handler`, first user block (`USER CODE BEGIN UsageFault_IRQn 0`):
```c
  uint32_t cfsr = SCB->CFSR;
  printf("\r\n*** UsageFault ***\r\n");
  printf("CFSR  = 0x%08lX\r\n", cfsr);
  if (cfsr & (1UL << 17)) printf("INVSTATE: tried to execute in ARM state (Thumb bit missing)\r\n");
  HAL_GPIO_WritePin(GPIOJ, GPIO_PIN_13, GPIO_PIN_SET);
```
Run. Expected output:
```c
Example 02: Execute Never (XN)
Step 1: XN off
Calling function at 0x2007F000 ...

*** UsageFault ***
CFSR  = 0x00020000
INVSTATE: tried to execute in ARM state (Thumb bit missing)
```

---------------------------
<br>

## Observations

### What we are testing

Execute Never (XN) is an MPU attribute. A region marked XN can hold data, but the CPU is not allowed to fetch instructions from it. In this example the same two instructions are copied into RAM and called twice. The only thing that changes between the two calls is the XN bit of the MPU region.

```c
/* Two Thumb instructions: movs r0, #42 ; bx lr */
static const uint16_t tiny_func[] = { 0x202A, 0x4770 };
```
The function is `movs r0, #42` followed by `bx lr`.      
It returns 42, so a successful run is easy to recognise.

If you don't understand what happend above `tiny_func` read this:

---------------

The CPU runs machine code, which is just numbers in memory. Each number is an instruction. `tiny_func` holds two instructions as raw numbers, so we can copy them into RAM and run them.

Instruction | Meaning | Hex
------------|---------|-----
`movs r0, #42` | Put the value `42` in register `r0` (`r0` holds a function’s return value) | `0x202A`
`bx lr` | Return to the caller (`lr` is the link register that holds the return address) | `0x4770`

So the function is “**return 42**”, written as `{ 0x202A, 0x4770 }`.

**How the text turns into hex**
Thumb instructions are 16 bits. The ARM architecture defines which bits mean what. Each instruction is a fixed pattern with some fields filled in.   
* `movs r0, #42`   
    The pattern for “move an 8-bit number into a register” is:
    ```
    0 0 1 0 0 | Rd (3 bits) | imm8 (8 bits)
    ```
    * `Rd` is the register number. `r0` is `000`.
    * `imm8` is the number. `42` is `0x2A`, which is `00101010`.
    ```
    00100 000 00101010
    = 0010 0000 0010 1010
    =  2    0    2    A    →  0x202A
    ```

* `bx lr`    
  The pattern for “branch to the address in a register” is:
  ```
  0 1 0 0 0 1 1 1 0 | Rm (4 bits) | 0 0 0
  ```
  * `Rm` is the register number. `lr` is `r14`, which is `1110`.
  ```
  010001110 1110 000
  = 0100 0111 0111 0000
  =  4    7    7    0    →  0x4770
  ```
  * Why `uint16_t`: each Thumb instruction is 16 bits wide, so each array element is one instruction.
  * Why the call used `CODE_ADDR | 1U`: the code is Thumb, and the core needs bit 0 of the branch address set to know that. The bit is only a flag. The first instruction is still at `0x2007F000`. That is the Step 6.

---------------


#### Step 1: XN off
```
Step 1: XN off
Calling function at 0x2007F000 ...
Returned 42
```

* The code was copied into RAM with ordinary data writes, and the region allows full data access.
* With XN off, the CPU is allowed to fetch instructions from that region, so the function ran and returned 42.
* This also shows that the default memory map allows code to run from SRAM. Nothing special is needed. That is exactly the risk: a stray jump into a data buffer would run its contents as code.

#### Step 2: XN on

```
Step 2: XN on
Calling function at 0x2007F000 ...

*** MemManage fault ***
CFSR  = 0x00000001
IACCVIOL: instruction access violation
```
* The bytes in RAM are identical to Step 1, and the address is the same. Only the region attribute changed.
* The CPU tried to fetch the first instruction, the MPU saw an instruction fetch to an XN region, and the CPU entered `MemManage_Handler`. "Not reached" is never printed.
* `CFSR = 0x00000001` means bit 0, **IACCVIOL**, is set. In Example 01 a data access gave bit 1 (**DACCVIOL**). So the two bits tell you which kind of access was blocked: bit 0 is an instruction fetch, bit 1 is a data access.
* There is no `MMFAR` line. For an instruction fetch fault, the MMARVALID bit (bit 7) is not normally set, so the fault address register is not valid. The address of the blocked instruction is the stacked PC in the exception frame, which the debugger shows as the frame below `<signal handler called>`. It equals `0x2007F000`.
* Copying the code never failed. XN does not protect the memory from being written. It only prevents execution from it.


#### A. Missing Thumb bit (Step 6: Forget the Thumb bit)

The function pointer was `CODE_ADDR` instead of `CODE_ADDR | 1`.

```
Example 02: Execute Never (XN)
Step 1: XN off
Calling function at 0x2007F000 ...

*** UsageFault ***
CFSR  = 0x00020000
INVSTATE: tried to execute in ARM state (Thumb bit missing)
```

* A Cortex-M can only run Thumb code. Bit 0 of a branch target tells the core which state to use. With bit 0 cleared, the core would have to switch to ARM state, which it does not support.
* With `USGFAULTENA` set the result is a **UsageFault** with `CFSR = 0x00020000`. Bit 17 is **INVSTATE**, "invalid state".
* This fault has nothing to do with the MPU. It happens even when the MPU is off, and it happens on the branch itself, before any code from RAM is fetched. That is why Step 2 is never reached.
* Without `USGFAULTENA`, the same fault **escalates to HardFault**. `HFSR` bit 30 (**FORCED**) is set and CFSR still shows the original cause. Our first attempt hit exactly this.

### Summary table

| Case | Result | CFSR | Meaning |
|---|---|---|---|
| XN off | `Returned 42` | none | Fetch allowed, code runs |
| XN on | MemManage fault | `0x00000001` | IACCVIOL: instruction fetch blocked by the MPU |
| No Thumb bit, UsageFault enabled | UsageFault | `0x00020000` | INVSTATE: not an MPU fault |
| No Thumb bit, UsageFault not enabled | HardFault | HFSR FORCED (bit 30) | Fault escalated because it was not enabled |
| No XN setting | `Returned 42` | none | Default map lets SRAM execute |

### key Takeaways

* Mark every region that should never hold code as XN: data buffers, DMA buffers, stacks, heaps, and peripheral and external memory windows.
* `IACCVIOL` (bit 0) means the CPU tried to execute. `DACCVIOL` (bit 1) means it tried to read or write data.
* Fault handlers must be enabled in `SCB->SHCSR`. A fault that is not enabled does not disappear, it escalates to HardFault.
* XN also stops speculative instruction fetches from the region. That is why the background region in the region layout uses Device and XN together.








