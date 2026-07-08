/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for FreeRTOS applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "can.h"
#include "tim.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef enum
{
    MOTOR_DIR_FORWARD = 0,
    MOTOR_DIR_BACKWARD
} MotorDirection;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*
 * 기존 컨베이어 서보모터 설정
 *
 * 연결:
 * PA0 = TIM2_CH1 = Servo Signal
 *
 * CubeMX TIM2 설정:
 * Prescaler      = 89
 * Counter Period = 19999
 * Pulse          = 1500
 *
 * 현재 APB1 Timer Clock = 90MHz 기준:
 * 90MHz / (89 + 1) = 1MHz
 * 1 tick = 1us
 *
 * Counter Period = 19999
 * 20000us = 20ms
 * PWM 주파수 = 50Hz
 *
 * 개조한 180도 서보를 연속회전 서보처럼 사용하는 방식:
 * 1500us 근처 = 정지
 * 2000us      = 한쪽 방향 회전
 * 1000us      = 반대 방향 회전
 */
#define CONVEYOR_SERVO_STOP_US      1520
#define CONVEYOR_SERVO_RUN_US       1900

/*
 * 모터A 45도 회전 설정
 *
 * 연결:
 * PA8  = IN1
 * PB10 = IN2
 * PB4  = IN3
 * PB5  = IN4
 *
 * 28BYJ-48 기준 대략값:
 * 4096 step = 360도
 * 512 step  = 45도
 * 1024 step = 90도
 */
#define MOTOR_A_45DEG_STEPS         512
#define MOTOR_A_STEP_DELAY_MS       1
#define MOTOR_A_HOLD_TIME_MS        2000

/*
 * 모터B DC모터 설정
 *
 * 연결:
 * PA6 = TIM3_CH1 = PWM
 * PA7 = IN1
 * PB6 = IN2
 *
 * CubeMX TIM3 설정:
 * Prescaler      = 89
 * Counter Period = 999
 *
 * TIM3도 APB1 Timer이고 현재 APB1 Timer Clock = 90MHz.
 * 90MHz / (89 + 1) = 1MHz
 * 1MHz / (999 + 1) = 1kHz
 *
 * PWM 값 범위:
 * 0 ~ 999
 */
#define MOTOR_B_PWM_STOP            0
#define MOTOR_B_PWM_RUN             700
#define MOTOR_B_RUN_TIME_MS         3000

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/*
 * main.c에 있는 전역 변수들을 가져와서 사용한다.
 */
extern volatile uint8_t g_conveyor_Stop;

extern volatile uint8_t g_motorA_Command;
extern volatile uint8_t g_motorA_Busy;

extern volatile uint8_t g_motorB_Command;
extern volatile uint8_t g_motorB_Busy;

/* USER CODE END Variables */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Definitions for ConveyorTask */
osThreadId_t ConveyorTaskHandle;
const osThreadAttr_t ConveyorTask_attributes = {
  .name = "ConveyorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for EventTask */
osThreadId_t EventTaskHandle;
const osThreadAttr_t EventTask_attributes = {
  .name = "EventTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for MotorBTask */
osThreadId_t MotorBTaskHandle;
const osThreadAttr_t MotorBTask_attributes = {
  .name = "MotorBTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for MotorCTask */
osThreadId_t MotorCTaskHandle;
const osThreadAttr_t MotorCTask_attributes = {
  .name = "MotorCTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Definitions for EventQueue */
osMessageQueueId_t EventQueueHandle;
const osMessageQueueAttr_t EventQueue_attributes = {
  .name = "EventQueue"
};

/* Definitions for TrashForwardTimer */
osTimerId_t TrashForwardTimerHandle;
const osTimerAttr_t TrashForwardTimer_attributes = {
  .name = "TrashForwardTimer"
};

/* Definitions for TrashBackwardTimer */
osTimerId_t TrashBackwardTimerHandle;
const osTimerAttr_t TrashBackwardTimer_attributes = {
  .name = "TrashBackwardTimer"
};

/* Definitions for CheckTimer */
osTimerId_t CheckTimerHandle;
const osTimerAttr_t CheckTimer_attributes = {
  .name = "CheckTimer"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/*
 * 기존 컨베이어 서보모터 함수
 */
void Conveyor_SetServoPulse(uint16_t pulse_us);
void Conveyor_Run(void);
void Conveyor_Stop(void);

/*
 * 모터A 스텝모터 함수
 */
void MotorA_Write(uint8_t in1, uint8_t in2, uint8_t in3, uint8_t in4);
void MotorA_Step(uint8_t stepIndex);
void MotorA_RotateSteps(uint16_t steps, MotorDirection direction, uint32_t delayMs);

/*
 * 모터B DC모터 함수
 */
void MotorB_SetPWM(uint16_t pwm);
void MotorB_DC_Start(void);
void MotorB_DC_Stop(void);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartTask04(void *argument);
void StartTask05(void *argument);
void TrashForwardTimerCallback(void *argument);
void TrashBackwardTimerCallback(void *argument);
void CheckTimerCallback(void *argument);

void MX_FREERTOS_Init(void);

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */

  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */

  /* USER CODE END RTOS_SEMAPHORES */

  /* Create the timer(s) */
  TrashForwardTimerHandle = osTimerNew(TrashForwardTimerCallback, osTimerOnce, NULL, &TrashForwardTimer_attributes);

  TrashBackwardTimerHandle = osTimerNew(TrashBackwardTimerCallback, osTimerOnce, NULL, &TrashBackwardTimer_attributes);

  CheckTimerHandle = osTimerNew(CheckTimerCallback, osTimerOnce, NULL, &CheckTimer_attributes);

  /* USER CODE BEGIN RTOS_TIMERS */

  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  EventQueueHandle = osMessageQueueNew(8, sizeof(uint8_t), &EventQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  ConveyorTaskHandle = osThreadNew(StartTask02, NULL, &ConveyorTask_attributes);

  EventTaskHandle = osThreadNew(StartTask03, NULL, &EventTask_attributes);

  MotorBTaskHandle = osThreadNew(StartTask04, NULL, &MotorBTask_attributes);

  MotorCTaskHandle = osThreadNew(StartTask05, NULL, &MotorCTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */

  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */

  /*
   * 현재 defaultTask는 특별한 동작 없이 유지한다.
   * 기존에 있던 CAN ID 0x126 송신 코드는 제거된 상태이다.
   */

  for (;;)
  {
    osDelay(1000);
  }

  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
  * @brief  Function implementing the ConveyorTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */

  /*
   * 기존 컨베이어 서보모터 담당 Task
   *
   * 연결:
   * PA0 = TIM2_CH1 = Servo Signal
   *
   * 요구사항:
   * 1. 보드 시작 시 기존 서보모터는 계속 회전
   * 2. CAN ID 0x124, Data[0] = 0xAA 수신 시 계속 정지
   * 3. CAN ID 0x124, Data[0] = 0x01 수신 시 다시 회전
   */

  uint8_t lastStopState = 255;

  /*
   * 기존 서보모터 PWM 시작
   */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

  Conveyor_Stop();
  osDelay(500);

  /*
   * 보드 시작 시 기존 서보모터는 계속 회전
   */
  g_conveyor_Stop = 0;
  Conveyor_Run();
  lastStopState = 0;

  for (;;)
  {
    /*
     * 이전 상태와 현재 상태가 달라졌을 때만
     * Run/Stop 처리를 한다.
     */
    if (g_conveyor_Stop != lastStopState)
    {
      lastStopState = g_conveyor_Stop;

      if (g_conveyor_Stop == 1)
      {
        Conveyor_Stop();
      }
      else
      {
        Conveyor_Run();
      }
    }

    osDelay(10);
  }

  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
  * @brief  Function implementing the EventTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */

  /*
   * 현재 EventTask는 사용하지 않는다.
   */

  for (;;)
  {
    osDelay(1000);
  }

  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartTask04 */
/**
  * @brief  Function implementing the MotorBTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartTask04 */
void StartTask04(void *argument)
{
  /* USER CODE BEGIN StartTask04 */

  /*
   * 모터B DC모터 담당 Task
   *
   * 연결:
   * PA6 = TIM3_CH1 = PWM
   * PA7 = IN1
   * PB6 = IN2
   *
   * CAN ID 0x124, Data[0] = 0x02 수신 시:
   * 모터B가 5초 동안 회전 후 정지한다.
   */

  /*
   * 모터B PWM 시작
   */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);

  MotorB_DC_Stop();

  for (;;)
  {
    if (g_motorB_Command == 1)
    {
      g_motorB_Command = 0;
      g_motorB_Busy = 1;

      MotorB_DC_Start();

      osDelay(MOTOR_B_RUN_TIME_MS);

      MotorB_DC_Stop();

      g_motorB_Busy = 0;
    }

    osDelay(10);
  }

  /* USER CODE END StartTask04 */
}

/* USER CODE BEGIN Header_StartTask05 */
/**
  * @brief  Function implementing the MotorCTask thread.
  * @param  argument: Not used
  */
/* USER CODE END Header_StartTask05 */
void StartTask05(void *argument)
{
  /* USER CODE BEGIN StartTask05 */

  /*
   * 모터A 스텝모터 담당 Task
   *
   * 연결:
   * PA8  = IN1
   * PB10 = IN2
   * PB4  = IN3
   * PB5  = IN4
   *
   * CAN ID 0x124, Data[0] = 0xAC 수신 시:
   * 1. 정방향 회전
   * 2. 2초 정지
   * 3. 역방향 회전해서 원위치 복귀
   */

  MotorA_Write(0, 0, 0, 0);

  for (;;)
  {
    if (g_motorA_Command == 1)
    {
      g_motorA_Command = 0;
      g_motorA_Busy = 1;

      MotorA_RotateSteps(MOTOR_A_45DEG_STEPS, MOTOR_DIR_FORWARD, MOTOR_A_STEP_DELAY_MS);

      osDelay(MOTOR_A_HOLD_TIME_MS);

      MotorA_RotateSteps(MOTOR_A_45DEG_STEPS, MOTOR_DIR_BACKWARD, MOTOR_A_STEP_DELAY_MS);

      MotorA_Write(0, 0, 0, 0);

      g_motorA_Busy = 0;
    }

    osDelay(1);
  }

  /* USER CODE END StartTask05 */
}

/* TrashForwardTimerCallback function */
void TrashForwardTimerCallback(void *argument)
{
  /* USER CODE BEGIN TrashForwardTimerCallback */

  /* USER CODE END TrashForwardTimerCallback */
}

/* TrashBackwardTimerCallback function */
void TrashBackwardTimerCallback(void *argument)
{
  /* USER CODE BEGIN TrashBackwardTimerCallback */

  /* USER CODE END TrashBackwardTimerCallback */
}

/* CheckTimerCallback function */
void CheckTimerCallback(void *argument)
{
  /* USER CODE BEGIN CheckTimerCallback */

  /* USER CODE END CheckTimerCallback */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/*
 * 기존 컨베이어 서보모터 PWM 펄스 설정
 *
 * 연결:
 * PA0 = TIM2_CH1 = Servo Signal
 *
 * TIM2 설정:
 * Prescaler      = 89
 * Counter Period = 19999
 *
 * 따라서 CCR 값 1500은 1500us, 즉 1.5ms 펄스를 의미한다.
 */
void Conveyor_SetServoPulse(uint16_t pulse_us)
{
  /*
   * 개조 서보/연속회전 서보는 보통 1000~2000us 범위에서 사용한다.
   */
  if (pulse_us < 1000)
  {
    pulse_us = 1000;
  }

  if (pulse_us > 2000)
  {
    pulse_us = 2000;
  }

  __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse_us);
}

/*
 * 기존 컨베이어 서보모터 회전
 *
 * 개조한 180도 서보를 연속회전 서보처럼 사용하는 방식.
 * 보통 1500보다 크면 한 방향으로 회전한다.
 */
void Conveyor_Run(void)
{
	  Conveyor_SetServoPulse(CONVEYOR_SERVO_RUN_US);
	  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
  //Conveyor_SetServoPulse(CONVEYOR_SERVO_RUN_US);
}

/*
 * 기존 컨베이어 서보모터 정지
 *
 * 보통 1500us 근처가 정지이다.
 * 개조 상태에 따라 완전 정지값이 1500에서 조금 벗어날 수 있다.
 */
void Conveyor_Stop(void)
{
	HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);
  //Conveyor_SetServoPulse(CONVEYOR_SERVO_STOP_US);
}

/*
 * 모터A 출력
 *
 * 연결:
 * PA8  = IN1
 * PB10 = IN2
 * PB4  = IN3
 * PB5  = IN4
 */
void MotorA_Write(uint8_t in1, uint8_t in2, uint8_t in3, uint8_t in4)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8,  in1 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, in2 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4,  in3 ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5,  in4 ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/*
 * 모터A의 한 스텝 출력
 */
void MotorA_Step(uint8_t stepIndex)
{
  switch (stepIndex)
  {
    case 0:
      MotorA_Write(1, 0, 0, 0);
      break;

    case 1:
      MotorA_Write(1, 1, 0, 0);
      break;

    case 2:
      MotorA_Write(0, 1, 0, 0);
      break;

    case 3:
      MotorA_Write(0, 1, 1, 0);
      break;

    case 4:
      MotorA_Write(0, 0, 1, 0);
      break;

    case 5:
      MotorA_Write(0, 0, 1, 1);
      break;

    case 6:
      MotorA_Write(0, 0, 0, 1);
      break;

    case 7:
      MotorA_Write(1, 0, 0, 1);
      break;

    default:
      MotorA_Write(0, 0, 0, 0);
      break;
  }
}

/*
 * 모터A를 원하는 스텝 수만큼 회전
 */
void MotorA_RotateSteps(uint16_t steps, MotorDirection direction, uint32_t delayMs)
{
  uint16_t i;

  for (i = 0; i < steps; i++)
  {
    uint8_t stepIndex;

    if (direction == MOTOR_DIR_FORWARD)
    {
      stepIndex = i % 8;
    }
    else
    {
      stepIndex = 7 - (i % 8);
    }

    MotorA_Step(stepIndex);
    osDelay(delayMs);
  }
}

/*
 * 모터B DC모터 PWM 설정
 *
 * 연결:
 * PA6 = TIM3_CH1 = PWM
 * PA7 = IN1
 * PB6 = IN2
 */
void MotorB_SetPWM(uint16_t pwm)
{
  if (pwm > 999)
  {
    pwm = 999;
  }

  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pwm);
}

/*
 * 모터B DC모터 시작
 *
 * IN1 = 1
 * IN2 = 0
 * PWM = MOTOR_B_PWM_RUN
 */
void MotorB_DC_Start(void)
{
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);

  MotorB_SetPWM(MOTOR_B_PWM_RUN);
}

/*
 * 모터B DC모터 정지
 *
 * IN1 = 0
 * IN2 = 0
 * PWM = 0
 */
void MotorB_DC_Stop(void)
{
  MotorB_SetPWM(MOTOR_B_PWM_STOP);

  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
}

/* USER CODE END Application */
