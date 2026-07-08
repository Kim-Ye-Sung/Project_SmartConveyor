/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "cmsis_os.h"
#include "can.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*
 * CAN ID 정의
 */
#define CAN_CONTROL_ID              0x124

#define SendID 						0x123
#define SendData					0xAB

/*
 * CAN ID 0x124 안에서 사용하는 데이터 정의
 */
#define CAN_DATA_STOP_CONVEYOR      0xAA
#define CAN_DATA_RUN_MOTOR_A        0xAC
#define CAN_DATA_RESUME_CONVEYOR    0x01
#define CAN_DATA_START_MOTOR_B      0x02

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

CAN_RxHeaderTypeDef RxHeader;
uint8_t RxData[8];

/*
 * 기존 컨베이어 서보모터 정지 상태 플래그
 *
 * 0: 기존 컨베이어 서보모터 회전
 * 1: 기존 컨베이어 서보모터 정지
 *
 * CAN ID 0x124, Data[0] = 0xAA 수신 시 1
 * CAN ID 0x124, Data[0] = 0x01 수신 시 0
 */
volatile uint8_t g_conveyor_Stop = 0;

/*
 * 모터A 동작 요청 플래그
 *
 * PA8, PB10, PB4, PB5에 연결된 스텝모터이다.
 *
 * CAN ID 0x124, Data[0] = 0xAC 수신 시 1
 */
volatile uint8_t g_motorA_Command = 0;

/*
 * 모터A가 현재 동작 중인지 표시
 *
 * 동작 중일 때 0xAC가 다시 들어와도 무시하기 위해 사용한다.
 */
volatile uint8_t g_motorA_Busy = 0;

/*
 * 모터B 동작 요청 플래그
 *
 * CAN ID 0x124, Data[0] = 0x02 수신 시 1
 */
volatile uint8_t g_motorB_Command = 0;

/*
 * 모터B가 현재 동작 중인지 표시
 *
 * 동작 중일 때 0x02가 다시 들어와도 무시하기 위해 사용한다.
 */
volatile uint8_t g_motorB_Busy = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);

/* USER CODE BEGIN PFP */

void CAN_Filter_Config(void);

void CAN_Send_StopAck(void);


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void CAN_Send_StopAck(void)
{
    CAN_TxHeaderTypeDef TxHeader;
    uint8_t TxData[1];
    uint32_t TxMailbox;

    TxHeader.StdId = SendID;          // 0x123
    TxHeader.IDE = CAN_ID_STD;
    TxHeader.RTR = CAN_RTR_DATA;
    TxHeader.DLC = 1;
    TxHeader.TransmitGlobalTime = DISABLE;

    TxData[0] = SendData;             // 0xAB

    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) > 0)
    {
        HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox);
    }
}


void CAN_Filter_Config(void)
{
    CAN_FilterTypeDef canFilter;

    canFilter.FilterBank = 0;
    canFilter.FilterMode = CAN_FILTERMODE_IDMASK;
    canFilter.FilterScale = CAN_FILTERSCALE_32BIT;

    /*
     * 현재 코드는 CAN ID 0x124만 통과시킨다.
     *
     * CAN ID 0x124 안의 RxData[0] 값으로
     * 0xAA, 0xAC, 0x01, 0x02 상황을 구분한다.
     */
    canFilter.FilterIdHigh = (CAN_CONTROL_ID << 5);
    canFilter.FilterIdLow = 0x0000;

    canFilter.FilterMaskIdHigh = (0x7FF << 5);
    canFilter.FilterMaskIdLow = 0x0000;

    canFilter.FilterFIFOAssignment = CAN_RX_FIFO0;
    canFilter.FilterActivation = ENABLE;
    canFilter.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan1, &canFilter) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    if (hcan->Instance == CAN1)
    {
        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
        {
            /*
             * CAN ID 0x124 수신
             *
             * Data[0] = 0xAA
             * → 기존 컨베이어 서보모터 정지
             *
             * Data[0] = 0xAC
             * → 모터A 스텝모터 동작
             *
             * Data[0] = 0x01
             * → 기존 컨베이어 서보모터 재가동
             *
             * Data[0] = 0x02
             * → 모터B DC모터 동작
             */
            if (RxHeader.StdId == CAN_CONTROL_ID)
            {
                if (RxData[0] == CAN_DATA_STOP_CONVEYOR)
                {
                    g_conveyor_Stop = 1;

                    /*
                     * 기존모터 정지 명령을 받았다는 것을
                     * CAN ID 0x123, Data[0] = 0xAB로 송신
                     */
                    CAN_Send_StopAck();
                }
                else if (RxData[0] == CAN_DATA_RUN_MOTOR_A)
                {
                    if ((g_motorA_Busy == 0) && (g_motorA_Command == 0))
                    {
                        g_motorA_Command = 1;
                    }
                }
                else if (RxData[0] == CAN_DATA_RESUME_CONVEYOR)
                {
                    g_conveyor_Stop = 0;
                }
                else if (RxData[0] == CAN_DATA_START_MOTOR_B)
                {
                    if ((g_motorB_Busy == 0) && (g_motorB_Command == 0))
                    {
                        g_motorB_Command = 1;
                    }
                }
            }
        }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_CAN1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();

  /* USER CODE BEGIN 2 */

  CAN_Filter_Config();

  if (HAL_CAN_Start(&hcan1) != HAL_OK)
  {
      Error_Handler();
  }

  if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
  {
      Error_Handler();
  }

  /* USER CODE END 2 */

  osKernelInitialize();
  MX_FREERTOS_Init();

  osKernelStart();

  while (1)
  {
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */

  __disable_irq();

  while (1)
  {
  }

  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */

  /* USER CODE END 6 */
}
#endif
