/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
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
#include "math.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "tim.h"
#include "can.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = { .name = "defaultTask",
		.stack_size = 128 * 4, .priority = (osPriority_t) osPriorityNormal, };
/* Definitions for motorTask */
osThreadId_t motorTaskHandle;
const osThreadAttr_t motorTask_attributes = { .name = "motorTask", .stack_size =
		256 * 4, .priority = (osPriority_t) osPriorityNormal, };
/* Definitions for canSemaphore */
osSemaphoreId_t canSemaphoreHandle;
const osSemaphoreAttr_t canSemaphore_attributes = { .name = "canSemaphore" };

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void Servo_SetAngle(uint32_t channel, uint16_t pulse_us);
void CAN_Send(uint32_t id, uint8_t data);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void motorTask01(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
 * @brief  FreeRTOS initialization
 * @param  None
 * @retval None
 */
void MX_FREERTOS_Init(void) {
	/* USER CODE BEGIN Init */

	/* USER CODE END Init */

	/* USER CODE BEGIN RTOS_MUTEX */
	/* add mutexes, ... */
	/* USER CODE END RTOS_MUTEX */

	/* Create the semaphores(s) */
	/* creation of canSemaphore */
	canSemaphoreHandle = osSemaphoreNew(1, 0, &canSemaphore_attributes);

	/* USER CODE BEGIN RTOS_SEMAPHORES */
	/* add semaphores, ... */
	/* USER CODE END RTOS_SEMAPHORES */

	/* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
	/* USER CODE END RTOS_TIMERS */

	/* USER CODE BEGIN RTOS_QUEUES */
	/* add queues, ... */
	/* USER CODE END RTOS_QUEUES */

	/* Create the thread(s) */
	/* creation of defaultTask */
	defaultTaskHandle = osThreadNew(StartDefaultTask, NULL,
			&defaultTask_attributes);

	/* creation of motorTask */
	motorTaskHandle = osThreadNew(motorTask01, NULL, &motorTask_attributes);

	/* USER CODE BEGIN RTOS_THREADS */
	/* add threads, ... */
	/* USER CODE END RTOS_THREADS */

	/* USER CODE BEGIN RTOS_EVENTS */
	/* add events, ... */
	/* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument) {
	/* USER CODE BEGIN StartDefaultTask */
	/* Infinite loop */
	for (;;) {
		if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET) {
			osSemaphoreRelease(canSemaphoreHandle);
			osDelay(500);
		}
		osDelay(10);
	}
	/* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_motorTask01 */
/**
 * @brief Function implementing the motorTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_motorTask01 */
void motorTask01(void *argument) {
	/* USER CODE BEGIN motorTask01 */
	/* Infinite loop */
	/* 펄스폭 정의 (1us 단위) */
	const uint16_t ARM_90 = 1500;  // 팔 기본 90도
	const uint16_t ARM_0 = 500;   // 팔 0도
	const uint16_t ARM_180 = 2500;  // 팔 180도
	const uint16_t GRIP_OPEN = 1500; // 그리퍼 열림(가정값)
	const uint16_t GRIP_CLOSE = 500;  // 그리퍼 닫힘/잡기(가정값)

	/* ── PWM 출력 시작 (태스크 안에서 직접 켜기) ── */
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);

	/* 시작 자세: 팔 90도, 그리퍼 열림 */
	Servo_SetAngle(TIM_CHANNEL_1, ARM_90);
	Servo_SetAngle(TIM_CHANNEL_2, GRIP_OPEN);
	osDelay(1000);

	for (;;) {

		/* 0x124 신호(세마포어) 올 때까지 대기 — 안 오면 여기서 블로킹 */
		if (osSemaphoreAcquire(canSemaphoreHandle, osWaitForever) == osOK) {
			HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
			osDelay(100);
			HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
			osDelay(100);
			float pos1 = ARM_90;
			/* 1. 팔 180도로 이동 1초로 고정*/
			/* 1. 팔 180도로 이동 - PD 제어 */
			{
				float target = ARM_180;
				float pos = ARM_90;     // 현재 위치(시작점)
				float Kp = 0.1f;          // 비례 게인
				float Kd = 0.05f;          // 미분 게인
				float prev_error = target - pos;

				for (;;) {
					float error = target - pos;
					float derivative = error - prev_error; // dt 일정(20ms)이라 차분으로 충분
					prev_error = error;

					if (error < 1.0f)
						break;                 // 충분히 도착하면 종료

					pos += Kp * error + Kd * derivative;     // PD 출력만큼 이동

					if (pos > ARM_180)
						pos = ARM_180;        // 오버슈트 클램프
					Servo_SetAngle(TIM_CHANNEL_1, (uint16_t) pos);
					osDelay(20);
				}
				Servo_SetAngle(TIM_CHANNEL_1, ARM_180);      // 끝값 정확히 보정
			}               // 이동 완료까지 대기

			/* 2. 그리퍼 닫기 (잡기) */
			Servo_SetAngle(TIM_CHANNEL_2, GRIP_CLOSE);
			osDelay(500);
			CAN_Send(0x124, 0x01);   // 1번 벨트 작동 신호
			CAN_Send(0x126, 0x01);   // 1번 벨트 작동 신호

			/* 3. 팔 0도로 이동 */

			for (float pos = ARM_180; pos >= ARM_0; pos = pos / 1.035) {
				if (pos <= 1) {
					pos = 0;
				}
				Servo_SetAngle(TIM_CHANNEL_1, (uint16_t) pos);
				osDelay(20);
			}

			/* 4. 그리퍼 열기 (놓기) */
			Servo_SetAngle(TIM_CHANNEL_2, GRIP_OPEN);
			osDelay(500);

			/* 5. 팔 90도로 복귀 */
			Servo_SetAngle(TIM_CHANNEL_1, ARM_90);
			osDelay(600);

			CAN_Send(0x124, 0x02);   // 2번 벨트 작동 신호
			CAN_Send(0x125, 0x01);   // 카메라(0x124)에게: 작업 완료
		}
	}
	/* USER CODE END motorTask01 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

