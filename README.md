
## <b>Edge_AI_licence_detection Example Description</b>

This example shows how to use the DCMIPP IP in continuous Mode as a Camera Serial Interface and it is based on the STM32Cube HAL API
that can be used to build any firmware application. Note that security is enabled by default on Cortex-M55.

- This project is targeted to run on STM32N657X0H3QU device on STM32N6570-DK board from STMicroelectronics.
- The project configures the maximum system clock frequency at 600 MHz.

- The project calls also SCB_EnableICache() and SCB_EnableDCache() functions in order to enable the Layer 1 Core Instruction and Data Caches.

This example describes how to configure the camera in continuous and Serial Modes in 800x480 resolution:
The used sensor OV5640 is configured with 800x480 resolution ,RGB565 Pixel Format and in serial Mode with the Virtual Channel0.
The CSI is configured with two physical data lanes and Data Type 8 for the Virtual Channel0.
The DCMIPP PIPE1 has been set up to select Data Type A which is programmed to choose RGB565. Additionally, it outputs RGB565 pixel format.

The frames are being captured through PIPE1 and then dumped into BUFFER_ADDRESS which acts as a storage area for the data that gets overwritten with each new frame
and displayed continuously through the LTDC.

- GREEN LED toggles after each frame acquisition
- RED LED is ON when an error occurs

This example uses UART logs with the following configuration : 115200 baud, 8 data bits, 1 stop bits and no parity.

#### <b>Notes</b>

 1. Care must be taken when using HAL_Delay(), this function provides accurate delay (in milliseconds)
    based on variable incremented in SysTick ISR. This implies that if HAL_Delay() is called from
    a peripheral ISR process, then the SysTick interrupt must have higher priority (numerically lower)
    than the peripheral interrupt. Otherwise the caller ISR process will be blocked.
    To change the SysTick interrupt priority you have to use HAL_NVIC_SetPriority() function.
 2. The application needs to ensure that the SysTick time base is always set to 1 millisecond
    to have correct HAL operation.

### <b>Keywords</b>

Graphic, DCMIPP, CSI, Pipe, Virtual Channel, Serial Mode, Continuous Mode, LTDC, Display

### <b>Directory contents</b>

      - DCMIPP/Edge_AI_licence_detection/FSBL/Src/main.c                         Main program
      - DCMIPP/Edge_AI_licence_detection/FSBL/Src/stm32n6xx_it.c                 Interrupt handlers
      - DCMIPP/Edge_AI_licence_detection/FSBL/Src/stm32n6xx_hal_msp.c            HAL MSP module
      - DCMIPP/Edge_AI_licence_detection/FSBL/Inc/main.h                         Main program header file
      - DCMIPP/Edge_AI_licence_detection/FSBL/Inc/stm32n6570_discovery_conf.h    BSP Configuration file
      - DCMIPP/Edge_AI_licence_detection/FSBL/Inc/stm32n6xx_hal_conf.h           HAL Configuration file
      - DCMIPP/Edge_AI_licence_detection/FSBL/Inc/stm32n6xx_it.h                 Interrupt handlers header file


### <b>Hardware and Software environment</b>

  - This project runs on STM32N657X0H3QU devices.
  - This project has been tested with STMicroelectronics STM32N6570-DK (MB1939)
    board and can be easily tailored to any other supported device
    and development board.

  - **EWARM** : To monitor a variable in the live watch window, you must proceed as follow :
    - Start a debugging session.
    - Open the View > Images.
    - Double-click to deselect the second instance of project.out.

  - **MDK-ARM** : To monitor a variable in the live watch window, you must comment out SCB_EnableDCache() in main() function.

### <b>How to use it ?</b>

In order to make the program work, you must do the following :

 - Set the boot mode in development mode (BOOT1 switch position is 1-3, BOOT0 switch position doesn't matter).
 - Open your preferred toolchain
 - Rebuild all files and load your image into target memory. Code can be executed in this mode for debugging purposes.

 Next, this program can be run in boot from flash mode. This is done by following the instructions below:

 - Resort to CubeProgrammer to add a header to the generated binary Project.bin with the following command
   - *STM32_SigningTool_CLI.exe -bin Project.bin -nk -of 0x80000000 -t fsbl -o Project-trusted.bin -hv 2.3 -dump Project-trusted.bin*
   - The resulting binary is Project-trusted.bin.
 - Next, in resorting again to CubeProgrammer, load the binary and its header (Project-trusted.bin) in DK board external Flash at address 0x7000'0000.
 - Set the boot mode in boot from external Flash (BOOT0 switch position is 1-2 and BOOT1 switch position is 1-2).
 - Press the reset button. The code then executes in boot from external Flash mode.












**Warning** If using CubeProgrammer v2.21 version or more recent, add *-align* option in the command line.
