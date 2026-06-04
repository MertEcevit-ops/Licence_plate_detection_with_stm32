/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_camera.h"
#include "app_resources.h"
#include "app_transport_uart.h"
#include "FreeRTOS.h"
#include "task.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ISP_WARMUP_FRAME_COUNT 60U
#define SNAPSHOT_CAPTURE_TIMEOUT_MS 3000U
#define USER_BUTTON_DEBOUNCE_MS 30U
#define USER_BUTTON_POLL_MS 10U
#define CAMERA_TASK_STACK_SIZE 2048U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

DCMIPP_HandleTypeDef hdcmipp;
LTDC_HandleTypeDef hltdc;
/* USER CODE BEGIN PV */
/* Calculate division factor for a given source and destination dimension (width or height) */
#define DIV_FACTOR(SRC, DST) (((uint32_t)((1024 * DST) / SRC)) > 1023 ? 1023 : ((uint32_t)((1024 * DST) / SRC)))

/* Calculate down scale ratio in unsigned 3.13 fixed-point format */
#define DOWNSCALE_RATIO(SRC, DST) (((uint32_t)(((float_t)(SRC) / (float_t)(DST)) * 8192) < 8192) ? 8192 : \
                                   ((((uint32_t)(((float_t)(SRC) / (float_t)(DST)) * 8192)) > 65535) ? 65535 : \
                                   ((uint32_t)(((float_t)(SRC) / (float_t)(DST)) * 8192))))
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_DCMIPP_Init(void);
static void LCD_Init(uint32_t Width, uint32_t Height);
/* USER CODE BEGIN PFP */
static void CameraTask(void *argument);
static void WaitForUserButtonPress(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_RED);
  if (BSP_PB_Init(BUTTON_USER1, BUTTON_MODE_GPIO) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }
    /* UART log */
#if USE_COM_LOG
  COM_InitTypeDef COM_Init;

  /* Initialize COM init structure */
  COM_Init.BaudRate   = 115200;
  COM_Init.WordLength = COM_WORDLENGTH_8B;
  COM_Init.StopBits   = COM_STOPBITS_1;
  COM_Init.Parity     = COM_PARITY_NONE;
  COM_Init.HwFlowCtl  = COM_HWCONTROL_NONE;

  BSP_COM_Init(COM1, &COM_Init);

  if (BSP_COM_SelectLogPort(COM1) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }
#endif
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_DCMIPP_Init();

  /* Initialize the IMX335 Sensor ----------------------------- */
  if (AppCamera_Probe(IMX335_R2592_1944, IMX335_RAW_RGGB10) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN 2 */
  if (AppResources_Init() != APP_STATUS_OK)
  {
    Error_Handler();
  }

  if (xTaskCreate(CameraTask, "Camera", CAMERA_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3U, NULL) != pdPASS)
  {
    Error_Handler();
  }

  vTaskStartScheduler();
  Error_Handler();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};

  /** Configure the System Power Supply */
  if (HAL_PWREx_ConfigSupply(PWR_EXTERNAL_SOURCE_SUPPLY ) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable HSI
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if(HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Get current CPU/System buses clocks configuration and
 if necessary switch to intermediate HSI clock to ensure target clock can be set
  */
  HAL_RCC_GetClockConfig(&RCC_ClkInitStruct);
  if((RCC_ClkInitStruct.CPUCLKSource == RCC_CPUCLKSOURCE_IC1) ||
     (RCC_ClkInitStruct.SYSCLKSource == RCC_SYSCLKSOURCE_IC2_IC6_IC11))
  {
    RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_CPUCLK|RCC_CLOCKTYPE_SYSCLK);
    RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_HSI;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_NONE;
  RCC_OscInitStruct.PLL1.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL1.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL1.PLLM = 4;
  RCC_OscInitStruct.PLL1.PLLN = 75;
  RCC_OscInitStruct.PLL1.PLLFractional = 0;
  RCC_OscInitStruct.PLL1.PLLP1 = 1;
  RCC_OscInitStruct.PLL1.PLLP2 = 1;
  RCC_OscInitStruct.PLL2.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL3.PLLState = RCC_PLL_NONE;
  RCC_OscInitStruct.PLL4.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_CPUCLK|RCC_CLOCKTYPE_HCLK
                              |RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1
                              |RCC_CLOCKTYPE_PCLK2|RCC_CLOCKTYPE_PCLK5
                              |RCC_CLOCKTYPE_PCLK4;
  RCC_ClkInitStruct.CPUCLKSource = RCC_CPUCLKSOURCE_IC1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_IC2_IC6_IC11;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;
  RCC_ClkInitStruct.APB5CLKDivider = RCC_APB5_DIV1;
  RCC_ClkInitStruct.IC1Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC1Selection.ClockDivider = 2;
  RCC_ClkInitStruct.IC2Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC2Selection.ClockDivider = 3;
  RCC_ClkInitStruct.IC6Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC6Selection.ClockDivider = 3;
  RCC_ClkInitStruct.IC11Selection.ClockSelection = RCC_ICCLKSOURCE_PLL1;
  RCC_ClkInitStruct.IC11Selection.ClockDivider = 3;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DCMIPP Initialization Function
  * @param None
  * @retval None
  */
static void MX_DCMIPP_Init(void)
{
  /* USER CODE BEGIN DCMIPP_Init 0 */
  /* USER CODE END DCMIPP_Init 0 */
  DCMIPP_PipeConfTypeDef pPipeConf = {0};
  DCMIPP_CSI_PIPE_ConfTypeDef pCSIPipeConf = {0};
  DCMIPP_CSI_ConfTypeDef csiconf = {0};
  DCMIPP_DownsizeTypeDef DonwsizeConf ={0};

  /* Set DCMIPP instance */
  hdcmipp.Instance = DCMIPP;
  if (HAL_DCMIPP_Init(&hdcmipp) != HAL_OK)
  {
    Error_Handler();
  }

  /* Configure the CSI */
  csiconf.DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES;
  csiconf.NumberOfLanes   = DCMIPP_CSI_TWO_DATA_LANES;
  csiconf.PHYBitrate      = DCMIPP_CSI_PHY_BT_1600;
  if(HAL_DCMIPP_CSI_SetConfig(&hdcmipp, &csiconf) != HAL_OK)
  {
    Error_Handler();
  }
  /* Configure the Virtual Channel 0 */
  /* Set Virtual Channel config */
  if(HAL_DCMIPP_CSI_SetVCConfig(&hdcmipp, DCMIPP_VIRTUAL_CHANNEL0, DCMIPP_CSI_DT_BPP10) != HAL_OK)
  {
    Error_Handler();
  }

  /* Configure the serial Pipe */
  pCSIPipeConf.DataTypeMode = DCMIPP_DTMODE_DTIDA;
  pCSIPipeConf.DataTypeIDA  = DCMIPP_DT_RAW10;
  pCSIPipeConf.DataTypeIDB  = DCMIPP_DT_RAW10; /* Don't Care */


  if (HAL_DCMIPP_CSI_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1, &pCSIPipeConf) != HAL_OK)
  {
    Error_Handler();
  }

  pPipeConf.FrameRate  = DCMIPP_FRAME_RATE_ALL;
  pPipeConf.PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;

  /* Set Pitch for Main and Ancillary Pipes */
  pPipeConf.PixelPipePitch  = 1600 ; /* Number of bytes */

  /* Configure Pipe */
  if (HAL_DCMIPP_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1, &pPipeConf) != HAL_OK)
  {
    Error_Handler();
  }

  /* Configure the downsize */
  DonwsizeConf.HSize       = 800; /* Destination Image Width  */
  DonwsizeConf.VSize       = 480; /* Destination Image Height */
  DonwsizeConf.HRatio      = DOWNSCALE_RATIO(IMX335_WIDTH, DonwsizeConf.HSize);
  DonwsizeConf.VRatio      = DOWNSCALE_RATIO(IMX335_HEIGHT, DonwsizeConf.VSize);
  DonwsizeConf.HDivFactor  = DIV_FACTOR(IMX335_WIDTH, DonwsizeConf.HSize);
  DonwsizeConf.VDivFactor  = DIV_FACTOR(IMX335_HEIGHT, DonwsizeConf.VSize);

  if(HAL_DCMIPP_PIPE_SetDownsizeConfig(&hdcmipp, DCMIPP_PIPE1, &DonwsizeConf) != HAL_OK)
  {
    Error_Handler();
  }
  if(HAL_DCMIPP_PIPE_EnableDownsize(&hdcmipp, DCMIPP_PIPE1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DCMIPP_Init 2 */
  /* USER CODE END DCMIPP_Init 2 */
}

static void LCD_Init(uint32_t Width, uint32_t Height)
{
  LTDC_LayerCfgTypeDef pLayerCfg ={0};

  hltdc.Instance = LTDC;

  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;

  hltdc.Init.HorizontalSync     = RK050HR18_HSYNC - 1;
  hltdc.Init.AccumulatedHBP     = RK050HR18_HSYNC + RK050HR18_HBP - 1;
  hltdc.Init.AccumulatedActiveW = RK050HR18_HSYNC + Width + RK050HR18_HBP -1;
  hltdc.Init.TotalWidth         = RK050HR18_HSYNC + Width + RK050HR18_HBP + RK050HR18_HFP - 1;
  hltdc.Init.VerticalSync       = RK050HR18_VSYNC - 1;
  hltdc.Init.AccumulatedVBP     = RK050HR18_VSYNC + RK050HR18_VBP - 1;
  hltdc.Init.AccumulatedActiveH = RK050HR18_VSYNC + Height + RK050HR18_VBP -1 ;
  hltdc.Init.TotalHeigh         = RK050HR18_VSYNC + Height + RK050HR18_VBP + RK050HR18_VFP - 1;

  hltdc.Init.Backcolor.Blue  = 0x0;
  hltdc.Init.Backcolor.Green = 0xFF;
  hltdc.Init.Backcolor.Red   = 0x0;

  if(HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }

  pLayerCfg.WindowX0       = 0;
  pLayerCfg.WindowX1       = Width;
  pLayerCfg.WindowY0       = 0;
  pLayerCfg.WindowY1       = Height;
  pLayerCfg.PixelFormat    = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.FBStartAdress  = BUFFER_ADDRESS;
  pLayerCfg.Alpha = LTDC_LxCACR_CONSTA;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  pLayerCfg.ImageWidth = Width;
  pLayerCfg.ImageHeight = Height;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if(HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, LTDC_LAYER_1))
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static void CameraTask(void *argument)
{
  UNUSED(argument);

  if (AppCamera_StartIspPreview(&hdcmipp) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  if (AppCamera_RunIspWarmup(ISP_WARMUP_FRAME_COUNT) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  if (AppCamera_StopPreview(&hdcmipp) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  LCD_Init(FRAME_WIDTH, FRAME_HEIGHT);

  if (AppCamera_ConfigureSnapshotDecimation(&hdcmipp, &hltdc) != APP_STATUS_OK)
  {
    Error_Handler();
  }

  for (;;)
  {
    WaitForUserButtonPress();

    if (AppCamera_CaptureSnapshot(&hdcmipp, BUFFER_ADDRESS,
                                  pdMS_TO_TICKS(SNAPSHOT_CAPTURE_TIMEOUT_MS)) != APP_STATUS_OK)
    {
      Error_Handler();
    }

    if (AppUartTransport_SendSnapshot(BUFFER_ADDRESS, SNAPSHOT_WIDTH,
                                      SNAPSHOT_HEIGHT, SNAPSHOT_DECIMATION_FACTOR) != APP_STATUS_OK)
    {
      Error_Handler();
    }

    BSP_LED_Toggle(LED_GREEN);
  }
}

static void WaitForUserButtonPress(void)
{
  while (BSP_PB_GetState(BUTTON_USER1) != BUTTON_PRESSED)
  {
    vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_POLL_MS));
  }

  vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_DEBOUNCE_MS));

  while (BSP_PB_GetState(BUTTON_USER1) == BUTTON_PRESSED)
  {
    vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_POLL_MS));
  }

  vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_DEBOUNCE_MS));
}

void HAL_DCMIPP_PIPE_FrameEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  AppCamera_OnPipeFrameEvent(hdcmipp, Pipe);
}

/**
 * @brief  Vsync Event callback on pipe
 * @param  hdcmipp DCMIPP device handle
 *         Pipe    Pipe receiving the callback
 * @retval None
 */
void HAL_DCMIPP_PIPE_VsyncEventCallback(DCMIPP_HandleTypeDef *hdcmipp, uint32_t Pipe)
{
  AppCamera_OnPipeVsyncEvent(hdcmipp, Pipe);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  while (1)
  {
    HAL_Delay(250);
    BSP_LED_Toggle(LED_RED);
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* Infinite loop */
  while (1)
  {
  }
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
