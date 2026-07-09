# SmartConveyor

SmartConveyor는 컨베이어벨트 위를 지나가는 물체의 QR 코드를 읽고, 그 결과에 따라 컨베이어와 로봇팔을 제어하는 프로젝트이다. 카메라가 QR을 인식하면 PC 또는 Raspberry Pi에서 먼저 판단하고, Arduino가 그 명령을 CAN 메시지로 바꿔 STM32 보드들에게 전달한다. 이후 STM32는 각자 맡은 모터를 움직이면서 물체를 멈추고, 잡고, 옮기고, 다시 벨트를 움직이는 흐름을 만든다.

처음부터 전체 시스템을 한 번에 만든 것은 아니고, CAN 통신 확인부터 시작해서 하나씩 붙여갔다. 먼저 보드끼리 메시지가 오가는지 확인했고, 그 다음 특정 ID를 받으면 모터가 멈추게 만들었다. 이후 같은 ID 안에서 Data 값을 다르게 보내 여러 동작을 구분했고, 마지막으로 QR 인식 프로그램과 로봇팔, 컨베이어 동작을 연결했다.

---

## 먼저 보는 전체 흐름

이 프로젝트의 핵심 흐름은 아래처럼 정리할 수 있다.

```text
카메라
  -> QR_Detect.cpp
     -> QR 값과 위치 확인
     -> Arduino로 UART 문자 전송

Arduino + MCP2515
  -> UART 문자를 CAN ID/Data로 변환
  -> CAN Bus에 송신

STM32 보드들
  -> stm_RobotArms: 로봇팔과 그리퍼 제어
  -> CAN_Motor: 컨베이어, 스텝모터, DC모터 제어

동작 완료 후
  -> STM32가 CAN으로 완료 신호 송신
  -> Arduino가 UART 문자로 다시 전달
  -> QR_Detect.cpp가 다음 물체를 받을 준비 상태로 변경
```

카메라가 모든 것을 직접 제어하지 않고, 카메라 쪽은 “무엇을 해야 하는지”만 판단한다. 실제 모터 제어는 STM32가 맡고, Arduino는 PC/Raspberry Pi와 CAN Bus 사이의 번역기 역할을 한다.

---

## 만든 이유

컨베이어벨트가 계속 돌기만 하면 물체를 분류할 수 없다. 반대로 카메라가 QR을 읽기만 하고 실제 모터가 움직이지 않으면 자동화 장치라고 보기 어렵다. 그래서 카메라 인식 결과가 실제 하드웨어 동작까지 이어지는 구조를 만들고 싶었다.

이 프로젝트에서 만들고 싶었던 동작은 다음과 같았다.

1. 컨베이어 위 물체의 QR을 읽는다.
2. 특정 QR이 들어오면 벨트를 멈춘다.
3. 로봇팔이 물체를 잡는다.
4. 첫 번째 벨트는 다시 움직인다.
5. 로봇팔이 물체를 다른 위치로 옮긴다.
6. 두 번째 벨트 또는 보조 모터가 동작한다.
7. 로봇팔과 카메라 프로그램은 다시 대기 상태로 돌아간다.

이 흐름을 만들기 위해 영상 인식, UART, CAN, FreeRTOS, PWM 모터 제어를 각각 연결했다.

---

## 사용한 것들

프로젝트는 크게 PC/Raspberry Pi 쪽 코드, Arduino 코드, STM32 코드로 나뉜다.

### PC 또는 Raspberry Pi

- `QR_Detect.cpp`
- OpenCV
- ZBar
- UART

카메라 영상을 받아오고 QR 코드를 인식한다. QR 값과 위치를 보고 Arduino로 문자 명령을 보낸다.

### Arduino

- `arduino.cpp`
- MCP2515 CAN 모듈
- `mcp_can` 라이브러리
- SPI

PC/Raspberry Pi에서 받은 UART 문자를 CAN 메시지로 바꾼다. 반대로 STM32에서 돌아오는 CAN 메시지도 다시 UART 문자로 바꿔 PC/Raspberry Pi에 알려준다.

### STM32

- STM32F446RE
- STM32CubeIDE
- STM32CubeMX
- HAL
- FreeRTOS
- CAN
- PWM / GPIO

CAN 메시지를 받아 실제 모터를 제어한다. `stm_RobotArms`는 로봇팔과 그리퍼를 맡고, `CAN_Motor`는 컨베이어와 보조 모터를 맡는다.

---

## 작업 순서

### 1. CAN 통신부터 확인

가장 먼저 한 것은 CAN 통신 연습이었다. 보드끼리 메시지가 오가는지 확인하지 않으면 이후 모터나 QR 인식을 붙여도 어디에서 문제가 생겼는지 알기 어렵기 때문이다.

`BaseCode/1_Turn_On_LED`에는 CAN 통신을 먼저 연습한 코드가 들어 있다. 이 단계에서는 메시지 수신 여부를 LED 같은 단순한 출력으로 확인했다.

### 2. 특정 CAN ID로 모터 정지

그 다음은 실제 모터를 연결했다. 보드에 전원을 넣으면 모터가 계속 돌고, 카메라 쪽에서 `0x124` ID가 오면 모터가 멈추도록 만들었다.

이 단계의 코드는 `BaseCode/2_0x124_Motor_Stop`에 있다. 여기서 중요한 것은 “CAN 메시지를 받았다”에서 끝나는 것이 아니라, 그 메시지로 실제 모터 동작이 바뀌었다는 점이다.

### 3. 같은 ID 안에서 Data 값으로 동작 구분

처음에는 `0x124`가 오면 모터를 멈추는 정도였다. 그런데 이후에는 같은 보드가 여러 동작을 해야 했다. 그래서 CAN ID는 유지하고, Data 값을 다르게 보내 명령을 구분했다.

```text
0x124 / 0xAA  -> 컨베이어 정지
0x124 / 0xAC  -> 스텝모터 동작
0x124 / 0x01  -> 컨베이어 재동작
0x124 / 0x02  -> DC모터 동작
```

이 단계의 코드는 `BaseCode/3_0x124_Data_Motor`에 있다. 이때부터 “ID는 누구에게 보내는지”, “Data는 무엇을 시킬지”에 가깝게 역할을 나눠서 생각했다.

### 4. 카메라 인식 결과를 CAN으로 연결

그 다음 QR 인식 프로그램을 붙였다. `QR_Detect.cpp`는 OpenCV로 카메라 프레임을 받아오고, ZBar로 QR 코드를 읽는다.

QR 코드가 보였다고 바로 명령을 보내지는 않았다. 컨베이어 위에서 물체는 계속 움직이고 있기 때문에, QR이 카메라에 처음 잡히는 순간과 실제로 처리해야 하는 위치가 다를 수 있다. 그래서 화면 높이의 약 2/3 지점에 기준선을 두고, QR 중심이 그 선 근처에 왔을 때만 명령을 보내도록 했다.

```cpp
int line_y = static_cast<int>(height * (2.0 / 3.0));

if (abs(centerY - line_y) < 20)
{
    if (data == "stop")
    {
        char cmd = 'A';
        write(uart_fd, &cmd, 1);

        cmd = 'S';
        write(uart_fd, &cmd, 1);
    }
}
```

QR 값은 현재 다음처럼 처리했다.

```text
stop   -> 로봇팔 동작 요청 + 컨베이어 정지
trash  -> 별도 분류 모터 동작 요청
기타   -> 통과
```

### 5. Arduino를 UART-CAN 변환기로 사용

PC/Raspberry Pi에서 CAN을 바로 다루기보다 Arduino와 MCP2515 CAN 모듈을 중간에 두었다. 이렇게 하면 QR 인식 프로그램은 문자 하나만 보내면 되고, CAN ID와 Data 구성은 Arduino에서 관리할 수 있다.

Arduino에서 사용한 변환 규칙은 다음과 같다.

```text
UART 'S' -> CAN 0x124, Data 0xAA
UART 'A' -> CAN 0x123, Data 0xAB
UART 'T' -> CAN 0x124, Data 0xAC

CAN 0x125 수신 -> UART 'D'
CAN 0x126 수신 -> UART 'R'
```

`S`는 Stop, `A`는 Arm, `T`는 Trash 쪽 동작으로 생각하고 나눴다. 반대로 `D`는 로봇팔 동작 완료, `R`은 벨트 재동작 가능 상태를 알려주는 문자로 사용했다.

### 6. 로봇팔과 컨베이어 흐름 합치기

`BaseCode/4_0x124_Merge_Success` 단계에서는 카메라, 컨베이어, 로봇팔 흐름을 합쳤다.

이때 중요했던 부분은 중복 명령 처리였다. 로봇팔이 아직 물체를 옮기는 중인데 카메라가 QR을 다시 읽으면, 벨트가 예상보다 빨리 다시 움직일 수 있다. 그래서 QR 인식 프로그램에는 `arm_is_run`, `is_belt_run` 같은 상태값을 두었고, STM32 쪽에도 모터가 동작 중인지 확인하는 Busy 플래그를 두었다.

### 7. 일부 모터를 DC모터 방식으로 변경

처음에는 벨트처럼 사용하는 부분에 스텝모터를 사용했다. 하지만 벨트 속도를 더 자유롭게 조절하려면 DC모터와 PWM 제어가 더 적합하다고 판단했다.

그래서 `BaseCode/5_0x124_DC_Mortor_Change` 단계에서 DC모터 방식으로 변경했다. DC모터는 TIM3 PWM과 방향 제어 핀을 사용한다.

```text
PA6 -> TIM3_CH1 PWM
PA7 -> IN1
PB6 -> IN2
```

PWM 값은 0~999 범위에서 사용했고, 현재 코드에서는 동작값을 700으로 두었다.

---

## QR 인식 프로그램

`QR_Detect.cpp`는 카메라 입력, QR 인식, UART 송수신을 담당한다.

프로그램 안에서 카메라 처리는 별도 스레드로 돌렸다. 카메라 프레임을 계속 읽고 QR을 계산하는 작업과, 화면 표시 및 UART 응답 확인을 같은 흐름에 넣으면 반응이 답답해질 수 있기 때문이다.

중요한 상태값은 다음과 같다.

```text
shared_frame  -> 화면에 보여줄 최신 프레임
is_running    -> 프로그램 실행 상태
arm_is_run    -> 로봇팔 동작 중복 방지
is_belt_run   -> 벨트 정지 명령 중복 방지
uart_fd       -> Arduino와 연결된 UART
```

카메라 화면에는 QR 테두리, QR 문자열, QR 중심점, 기준선을 표시한다. 실제 문서에 실행 화면을 넣는다면 이 부분이 가장 먼저 들어가면 좋을 것 같다.

```text
[이미지 추가 위치]
Screenshot/QRDetectScreen.png
카메라 화면에 QR 박스, QR 데이터, 빨간 기준선이 보이는 장면
```

---

## CAN 메시지 약속

CAN 메시지는 여러 장치가 같은 선을 공유하기 때문에 약속을 명확히 정해야 했다. 이 프로젝트에서는 ID로 대상을 구분하고, Data로 동작을 구분했다.

```text
0x123
  - 받는 쪽: stm_RobotArms
  - 의미: 로봇팔 동작 요청

0x124
  - 받는 쪽: CAN_Motor
  - 의미: 컨베이어, 스텝모터, DC모터 제어

0x125
  - 보내는 쪽: stm_RobotArms
  - 의미: 로봇팔 동작 완료

0x126
  - 보내는 쪽: stm_RobotArms
  - 의미: 벨트 재동작 가능
```

STM32에서 CAN 필터를 설정할 때 표준 ID는 그대로 넣지 않고 `<< 5`를 해주어야 했다.

```c
canFilter.FilterIdHigh = (CAN_CONTROL_ID << 5);
canFilter.FilterMaskIdHigh = (0x7FF << 5);
```

처음에는 ID 값만 맞추면 된다고 생각하기 쉬운데, STM32 HAL CAN 필터에서는 레지스터 위치에 맞춰 정렬해야 한다. 이 부분은 실제로 CAN 수신을 확인하면서 정리한 내용이다.

---

## STM32 설정

STM32F446RE는 HSI 16MHz를 PLL로 올려 180MHz로 사용했다.

```text
HSI 16MHz / PLLM 8 * PLLN 180 / PLLP 2 = 180MHz
```

APB1은 45MHz이고, CAN1은 APB1에 연결되어 있다. 그래서 CAN 속도 계산은 APB1 45MHz를 기준으로 했다.

`stm_RobotArms`는 다음 설정으로 500kbps를 맞췄다.

```text
45MHz / Prescaler 9 / (1 + BS1 7 + BS2 2) = 500kbps
```

`CAN_Motor`는 다음 설정으로 500kbps가 된다.

```text
45MHz / Prescaler 5 / (1 + BS1 15 + BS2 2) = 500kbps
```

CAN 통신에서는 버스에 연결된 장치들의 속도가 같아야 한다. 한 보드라도 속도가 다르면 메시지가 제대로 들어오지 않는다.

---

## FreeRTOS를 적용한 방식

STM32 쪽은 FreeRTOS를 사용했다. 모터 제어를 모두 하나의 반복문 안에서 처리하면 한 동작이 진행되는 동안 다른 상태를 확인하기 어렵다. 그래서 역할별로 태스크를 나누었다.

`stm_RobotArms`에서는 로봇팔 태스크가 세마포어를 기다린다. CAN 메시지가 들어오면 수신 콜백에서 세마포어를 release하고, 로봇팔 태스크가 깨어나 동작한다.

```c
if (osSemaphoreAcquire(canSemaphoreHandle, osWaitForever) == osOK)
{
    // 로봇팔 동작
}
```

`CAN_Motor`에서는 CAN 수신 콜백이 플래그를 바꾸고, 각 태스크가 그 플래그를 보고 동작한다.

```text
ConveyorTask -> 컨베이어 정지/재동작
MotorBTask   -> DC모터 일정 시간 동작
MotorCTask   -> 스텝모터 회전 후 복귀
```

인터럽트 안에서 모터를 오래 돌리지 않고, 인터럽트는 “명령이 왔다”는 표시만 남기도록 했다. 실제로 시간이 걸리는 동작은 태스크에서 처리한다.

---

## 모터 동작 정리

### 컨베이어

컨베이어는 `CAN_Motor`에서 TIM2 CH1 PWM으로 제어한다. 신호선은 PA0이다.

```text
TIM2 Prescaler: 89
TIM2 Period:    19999
PWM 주기:       20ms
PWM 주파수:     50Hz
```

연속회전 서보처럼 사용했기 때문에 1500us 근처는 정지, 1900us는 회전으로 두었다.

```c
#define CONVEYOR_SERVO_STOP_US 1520
#define CONVEYOR_SERVO_RUN_US  1900
```

### 로봇팔과 그리퍼

로봇팔은 `stm_RobotArms`에서 제어한다.

```text
TIM2_CH1 -> 로봇팔
TIM2_CH2 -> 그리퍼
```

동작 순서는 다음과 같다.

```text
대기
  -> CAN 0x123 수신
  -> 팔 이동
  -> 그리퍼 닫기
  -> 0x124 / 0x01 송신
  -> 팔 반대 위치로 이동
  -> 그리퍼 열기
  -> 기본 위치 복귀
  -> 0x124 / 0x02 송신
  -> 0x125 / 0x01 송신
```

로봇팔 이동은 일부 구간에서 목표 펄스폭까지 조금씩 접근하도록 했다. 센서 피드백을 받는 완전한 제어는 아니지만, 한 번에 큰 각도로 움직이는 것보다 동작이 부드럽게 보이도록 하기 위한 방식이다.

### 스텝모터

스텝모터는 `0x124 / 0xAC` 명령을 받으면 동작한다.

```text
PA8  -> IN1
PB10 -> IN2
PB4  -> IN3
PB5  -> IN4
```

정방향으로 회전하고, 잠시 멈춘 뒤 역방향으로 다시 돌아온다. 동작 중 같은 명령이 다시 들어오면 Busy 플래그로 무시하도록 했다.

### DC모터

DC모터는 `0x124 / 0x02` 명령을 받으면 동작한다.

```text
PA6 -> PWM
PA7 -> IN1
PB6 -> IN2
```

TIM3를 사용해 1kHz PWM을 만들고, 일정 시간 회전한 뒤 정지한다.

---

## 실제 동작 순서

전체 시스템이 한 번 동작할 때의 흐름은 아래와 같다.

```text
1. QR_Detect.cpp가 카메라에서 QR을 읽는다.
2. QR 중심이 기준선 근처인지 확인한다.
3. QR 값이 stop이면 Arduino로 'S'와 'A'를 보낸다.
4. Arduino는 'S'를 0x124 / 0xAA로 바꿔 CAN 송신한다.
5. CAN_Motor는 컨베이어를 정지한다.
6. Arduino는 'A'를 0x123 / 0xAB로 바꿔 CAN 송신한다.
7. stm_RobotArms는 로봇팔과 그리퍼를 동작시킨다.
8. 로봇팔이 물체를 잡은 뒤 0x124 / 0x01을 보낸다.
9. CAN_Motor는 첫 번째 컨베이어를 다시 움직인다.
10. 로봇팔이 물체를 옮긴 뒤 0x124 / 0x02를 보낸다.
11. CAN_Motor는 두 번째 모터를 동작시킨다.
12. 로봇팔은 0x125를 보내 완료를 알린다.
13. Arduino는 완료 신호를 UART 'D'로 바꿔 QR_Detect.cpp에 전달한다.
14. QR_Detect.cpp는 로봇팔 상태를 다시 대기 상태로 바꾼다.
```

이 흐름에서 중요한 것은 각 장치가 자기 역할만 맡는다는 점이다. QR 인식 프로그램은 판단만 하고, Arduino는 변환만 하고, STM32는 실제 동작을 처리한다.

---

## 실행 방법

### QR 인식 프로그램

루트 폴더에서 빌드한다.

```bash
make
```

실행 파일은 `qr_scanner`로 생성된다.

```bash
./qr_scanner
```

OpenCV4와 ZBar가 필요하다. `Makefile`에서는 OpenCV 옵션을 `pkg-config`로 가져오고, ZBar는 `-lzbar`로 링크한다.

UART 포트는 현재 `/dev/ttyACM0`로 되어 있다.

```cpp
uart_fd = init_uart("/dev/ttyACM0", B115200);
```

Arduino가 다른 포트로 잡히면 `/dev/ttyUSB0`처럼 실제 포트에 맞게 바꿔야 한다.

### Arduino

`arduino.cpp`를 Arduino에 업로드한다. MCP2515 CAN 모듈은 SPI로 연결하고, CS 핀은 10번으로 설정했다.

```cpp
const int SPI_CS_PIN = 10;
MCP_CAN CAN(SPI_CS_PIN);
```

CAN 속도는 500kbps로 맞췄다.

```cpp
CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ)
```

### STM32

STM32CubeIDE에서 아래 두 프로젝트를 각각 열어 빌드하고 업로드한다.

```text
CAN_Motor      -> 컨베이어, 스텝모터, DC모터 제어
stm_RobotArms  -> 로봇팔, 그리퍼 제어
```

두 STM32 보드는 CAN_H, CAN_L을 같은 CAN 버스에 연결해야 한다. GND도 공통으로 연결해야 한다.

---

## 확인한 기능

프로젝트에서 확인한 기능은 다음과 같다.

```text
QR 인식
  - 카메라 화면에서 QR 문자열을 읽음
  - QR 중심과 기준선 위치를 비교함

UART 통신
  - QR 결과에 따라 Arduino로 A/S/T 전송
  - Arduino에서 D/R 응답 수신

CAN 통신
  - Arduino가 UART 명령을 CAN 메시지로 변환
  - STM32가 필요한 CAN ID만 필터링해서 수신

모터 제어
  - 0x124 / 0xAA 수신 시 컨베이어 정지
  - 0x124 / 0x01 수신 시 컨베이어 재동작
  - 0x123 수신 시 로봇팔 동작
  - 0x124 / 0xAC 수신 시 스텝모터 동작
  - 0x124 / 0x02 수신 시 DC모터 동작

상태 관리
  - 로봇팔 동작 중 중복 명령 방지
  - 모터 동작 중 중복 명령 방지
```

---

## 폴더 구조

```text
Project_SmartConveyor
├─ QR_Detect.cpp
├─ arduino.cpp
├─ Makefile
├─ README.md
├─ README2.md
├─ ConveyorBelt_Project
│  ├─ README.md
│  └─ image.png
├─ BaseCode
│  ├─ 1_Turn_On_LED
│  ├─ 2_0x124_Motor_Stop
│  ├─ 3_0x124_Data_Motor
│  ├─ 4_0x124_Merge_Success
│  └─ 5_0x124_DC_Mortor_Change
├─ CAN_Motor
│  ├─ CAN_Motor.ioc
│  ├─ Core
│  ├─ Drivers
│  └─ Middlewares
└─ stm_RobotArms
   ├─ stm_RobotArms.ioc
   ├─ Core
   ├─ Drivers
   └─ Middlewares
```

---

## 앞으로 수정하면 좋을 부분

현재는 전체 흐름을 확인하는 데 초점을 맞췄기 때문에, 더 다듬을 수 있는 부분도 남아 있다.

```text
QR 데이터 규칙
  - 현재는 stop, trash처럼 고정 문자열 중심이다.
  - QR 종류가 늘어나면 명령 규칙을 따로 정리하는 것이 좋다.

UART 포트
  - /dev/ttyACM0가 코드에 직접 들어가 있다.
  - 실행 인자나 설정 파일로 빼면 다른 환경에서 쓰기 편하다.

로봇팔 위치값
  - PWM 펄스폭이 코드에 고정되어 있다.
  - 실제 장치 위치에 맞춘 캘리브레이션 값으로 분리하면 좋다.

CAN 에러 처리
  - 현재는 정상 흐름 중심으로 작성되어 있다.
  - 송신 실패나 버스 오류 상황에 대한 처리를 추가할 수 있다.

실행 자료
  - QR 인식 화면, 전체 하드웨어 사진, 로봇팔 동작 영상을 추가하면 이해하기 쉽다.
```

---

## 정리

SmartConveyor는 QR 인식과 임베디드 제어를 연결한 프로젝트이다. 카메라에서 QR을 읽고, UART로 Arduino에 전달하고, Arduino가 CAN 메시지로 바꾸고, STM32가 실제 모터를 움직인다.

이 프로젝트를 진행하면서 가장 중요했던 부분은 각 장치의 역할을 나누는 것이었다. QR 인식 프로그램은 판단만 맡고, Arduino는 통신 변환을 맡고, STM32는 실제 제어를 맡도록 나누었다. 이렇게 나누니 문제가 생겼을 때 어느 구간을 확인해야 하는지도 더 분명해졌다.

또한 CAN 통신에서는 ID와 Data를 어떻게 나눌지, FreeRTOS에서는 인터럽트와 태스크 역할을 어떻게 나눌지가 중요했다. 단순히 모터를 돌리는 것보다, 여러 장치가 순서대로 신호를 주고받으며 하나의 동작처럼 이어지게 만드는 과정이 핵심이었다.
