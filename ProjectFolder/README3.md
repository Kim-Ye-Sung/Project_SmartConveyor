# Smart Conveyor

> **QR 인식 결과를 CAN 네트워크의 제어 명령으로 바꾸어, 컨베이어와 로봇팔이 협업하도록 만든 자동 분류 시스템**

`Embedded Vision` · `C++17` · `OpenCV` · `ZBar` · `STM32F446RE` · `FreeRTOS` · `CAN 500 kbps`

카메라가 컨베이어 위 물체의 QR 코드를 읽으면 Linux 제어기가 처리 경로를 결정합니다. 판정 결과는 UART-CAN 게이트웨이를 거쳐 두 대의 STM32 제어 노드로 전달되고, 각 노드는 FreeRTOS 태스크로 로봇팔·컨베이어·분류 모터를 독립 제어합니다.

이 프로젝트의 핵심은 개별 모터를 움직이는 데 그치지 않고 **인식 → 판단 → 통신 → 동작 → 완료 통보**가 하나의 공정으로 이어지게 만든 것입니다.

---

## System at a glance

![Smart Conveyor 시스템 아키텍처](./Screenshot/SmartConveyor_Architecture.png)

| 항목 | 구현 내용 |
|---|---|
| 입력 | USB 카메라로 촬영한 컨베이어 영상과 QR 데이터 |
| 판단 | QR 중심이 화면 판정선에 도달했을 때 `stop`, `trash`, 그 외 값으로 분류 |
| 상위 제어 | Linux C++ 프로그램에서 영상 처리, 상태 관리, UART 통신 수행 |
| 게이트웨이 | Arduino와 MCP2515가 UART 문자와 CAN 프레임을 양방향 변환 |
| 하위 제어 | STM32F446RE 두 대가 로봇팔 노드와 컨베이어 노드 역할을 분담 |
| 실시간 처리 | FreeRTOS 태스크, CAN 수신 인터럽트, 세마포어, busy 플래그 사용 |
| 구동부 | 서보모터, 연속회전 서보, 28BYJ-48 스텝모터, DC 모터 |

> [!NOTE]
> 위 구성도는 프로젝트 소스의 최종 통신 ID, RTOS 구조, 구동부 구성을 바탕으로 새로 제작한 시스템 아키텍처 이미지입니다.

---

## Demo

| QR 인식과 판정선 | 로봇팔 픽업 |
|---|---|
| ![QR 코드 인식 화면](./Screenshot/QR인식사진.png) | ![로봇팔 픽업 동작](./Screenshot/로봇팔잡기사진.png) |

### 실행 영상

- [전체 시스템 실행 영상](./Screenshot/전체실행영상.mp4)
- [카메라 QR 인식 영상](./Screenshot/카메라QR영상.mp4)

> [!IMPORTANT]
> **영상 교체 위치**  
> GitHub의 README 또는 Issue 편집 화면에 영상을 드래그해 업로드하고, 생성된 `github.com/user-attachments/...` 주소를 아래 줄에 붙여 넣으세요. 저장소의 MP4 파일 링크보다 방문자가 바로 재생하기 좋습니다.
>
> `여기에 전체 실행 영상 URL 삽입`

---

## Project snapshot

| 구분 | 내용 |
|---|---|
| 프로젝트 형태 | 팀 프로젝트 |
| 개발 기간 | 2026.06 |
| 개발 환경 | Raspberry Pi OS 또는 Ubuntu, STM32CubeIDE, Arduino IDE |
| 개발 언어 | C, C++17, Arduino C++ |
| 통신 | UART 115200 bps, CAN 500 kbps |
| MCU | STM32F446RE × 2 |
| 핵심 라이브러리 | OpenCV, ZBar, STM32 HAL, CMSIS-RTOS2 |

> [!CAUTION]
> **제출 전 직접 작성할 항목**  
> `팀 인원`, `정확한 개발 기간`, `본인 담당 범위`를 실제 프로젝트 내용에 맞게 추가하세요. 저장소만으로 개인별 기여도를 확정할 수 없어 임의로 작성하지 않았습니다.

---

## 분류 규칙

카메라에 QR이 보였다는 이유만으로 즉시 동작하지 않습니다. QR 중심이 설정한 판정선에 도달했을 때만 이벤트를 만들고, 데이터에 따라 다음 경로로 분기합니다.

| QR 데이터 | 처리 경로 | 결과 |
|---|---|---|
| `stop` | Robot Arm Path | 메인 벨트 정지 → 물체 픽업·이송 → 벨트 재가동 |
| `trash` | Stepper Sorter Path | 분류판 정회전 → 2초 유지 → 역회전 복귀 |
| 그 외 값 | Pass Path | 모터 명령 없이 다음 공정으로 통과 |

---

## 동작 시나리오

### Scenario 01. 정상 물체 통과

`stop`, `trash`가 아닌 QR은 화면에 `pass`로 표시하고 별도의 UART·CAN 명령을 보내지 않습니다.

~~~text
QR 인식 → 판정선 도달 → 일반 데이터 확인 → "pass" 출력 → 메인 벨트로 계속 이송
~~~

| 구동부 | 동작 상태 |
|---|---|
| 메인 컨베이어 | 계속 회전 |
| 로봇팔 | 90° 대기 자세 유지 |
| 스텝모터 분류판 | 원위치 유지 |
| DC 모터 벨트 | 정지 상태 유지 |

### Scenario 02. `trash` 물체 분류

`trash`는 메인 벨트를 멈추지 않고 스텝모터 분류판만 왕복시켜 별도 경로로 보냅니다.

| 순서 | 발생 위치 | 처리 내용 |
|---:|---|---|
| 1 | Linux | QR 문자열 `trash`와 판정선 도달을 확인합니다. |
| 2 | Linux → Arduino | UART 문자 `T`를 전송합니다. |
| 3 | Arduino → Conveyor | `0x124/0xAC` CAN 프레임으로 변환합니다. |
| 4 | Conveyor CAN ISR | 모터가 대기 중이면 `g_motorA_Command`를 설정합니다. 동작 중이면 busy 상태로 중복 명령을 무시합니다. |
| 5 | `MotorCTask` | 분류판을 512스텝 정방향으로 회전시킵니다. |
| 6 | `MotorCTask` | 2초간 위치를 유지한 뒤 512스텝 역방향으로 회전합니다. |
| 7 | `MotorCTask` | GPIO 출력을 해제하고 busy 상태를 풀어 다음 물체를 기다립니다. |

~~~text
trash → UART 'T' → CAN 0x124/0xAC → 분류판 45° 이동 → 2초 유지 → 원위치 복귀
~~~

### Scenario 03. `stop` 물체 로봇팔 이송

`stop`은 메인 벨트를 정지시킨 뒤 로봇팔이 물체를 집어 보조 벨트로 옮기는 전체 협업 시나리오입니다.

| 순서 | 발생 위치 | 처리 내용 |
|---:|---|---|
| 1 | Linux | QR 문자열 `stop`과 판정선 도달을 확인합니다. |
| 2 | Linux → Arduino | UART로 `A`와 `S`를 전송합니다. |
| 3 | Arduino → CAN | `0x123/0xAB`로 로봇팔을 시작하고 `0x124/0xAA`로 벨트를 정지합니다. |
| 4 | Robot Arm STM32 | CAN ISR이 세마포어를 release하여 로봇팔 태스크를 깨웁니다. |
| 5 | Robot Arm STM32 | 팔을 90°에서 180° 방향으로 이동시키고 그리퍼를 닫아 물체를 집습니다. |
| 6 | Robot Arm → Conveyor | `0x124/0x01`을 보내 메인 컨베이어를 재가동합니다. |
| 7 | Robot Arm → Linux | `0x126/0x01`이 Arduino에서 UART `R`로 변환되어 벨트 재동작 상태를 알립니다. |
| 8 | Robot Arm STM32 | 팔을 반대편으로 이동하고 그리퍼를 열어 물체를 내려놓은 뒤 90°로 복귀합니다. |
| 9 | Robot Arm → Conveyor | `0x124/0x02`를 보내 DC 모터 벨트를 3초간 구동합니다. |
| 10 | Robot Arm → Linux | `0x125/0x01`이 Arduino에서 UART `D`로 변환되어 작업 완료를 알립니다. |
| 11 | Linux | `arm_is_run` 상태를 해제하고 다음 물체를 기다립니다. |

~~~text
stop → 로봇팔 시작 + 벨트 정지 → 픽업 → 벨트 재가동 → 이송·배치 → 보조 벨트 구동 → 완료 통보
~~~

### 시나리오 종료 후 상태

| 시나리오 | 메인 벨트 | 로봇팔 | 분류판 | 보조 벨트 | 다음 처리 준비 |
|---|---|---|---|---|---|
| 정상 통과 | 계속 회전 | 90° 대기 | 원위치 | 정지 | 즉시 가능 |
| `trash` | 계속 회전 | 90° 대기 | 왕복 후 원위치 | 정지 | busy 해제 후 가능 |
| `stop` | 정지 후 재가동 | 이송 후 90° 복귀 | 원위치 | 3초 구동 후 정지 | 완료 신호 수신 후 가능 |

---

## Engineering highlights

### 1. QR 검출을 실제 제어 이벤트로 변환

영상 처리 스레드는 카메라 프레임을 640×480으로 입력받아 그레이스케일로 변환하고 ZBar에 전달합니다. 인식된 QR의 네 꼭짓점 평균으로 중심을 계산한 뒤 화면 높이의 2/3 지점에 둔 판정선과 비교합니다.

~~~cpp
int line_y = static_cast<int>(height * (2.0 / 3.0));

if (abs(centerY - line_y) < 20)
{
    if (data == "stop")
    {
        // 로봇팔 시작 + 메인 벨트 정지
    }
    else if (data == "trash")
    {
        // 스텝모터 분류 동작
    }
}
~~~

이 방식으로 QR이 프레임에 처음 등장한 시점이 아니라 **실제 분류 지점에 도착한 시점**을 제어 이벤트로 사용했습니다.

영상 처리와 화면 표시·UART 수신은 별도 스레드로 분리했습니다.

- `vision_thread_func`: 카메라 캡처, QR 디코딩, 위치 판정
- `main thread`: 화면 표시, 종료 입력, Arduino 상태 수신
- `mutex`: 두 스레드가 공유하는 프레임 보호
- `atomic<bool>`: 프로그램, 로봇팔, 벨트 상태 공유
- `O_NONBLOCK`: UART 응답이 없을 때도 화면 루프 유지

구현 근거: [`QR_Detect.cpp`](./QR_Detect.cpp)

### 2. Linux와 CAN 버스 사이의 작은 게이트웨이

Linux 프로그램이 CAN 하드웨어를 직접 다루게 하지 않고, Arduino에 단순한 문자 명령만 전달하도록 경계를 나눴습니다.

~~~text
Linux C++  ── UART 문자 ──>  Arduino + MCP2515  ── CAN Frame ──>  STM32 Nodes
Linux C++  <── 상태 문자 ──  Arduino + MCP2515  <── CAN Frame ──  STM32 Nodes
~~~

Arduino는 세 종류의 상위 명령을 CAN으로 변환하고, 로봇팔의 완료·재가동 프레임을 다시 Linux 상태 문자로 변환합니다. 덕분에 영상 처리 코드는 CAN 레지스터와 하드웨어 의존성을 알 필요가 없습니다.

구현 근거: [`arduino.cpp`](./arduino.cpp)

### 3. CAN ID와 데이터 바이트로 명령 채널 설계

CAN 버스에 연결된 모든 노드는 500 kbps로 통신합니다. 명령 종류는 표준 ID와 `Data[0]` 조합으로 구분했습니다.

| 방향 | UART | CAN ID | Data[0] | 명령 |
|---|---:|---:|---:|---|
| Linux → Robot Arm | `A` | `0x123` | `0xAB` | 픽앤플레이스 시작 |
| Linux → Conveyor | `S` | `0x124` | `0xAA` | 메인 컨베이어 정지 |
| Linux → Conveyor | `T` | `0x124` | `0xAC` | 스텝모터 분류 시작 |
| Robot Arm → Conveyor | - | `0x124` | `0x01` | 메인 컨베이어 재가동 |
| Robot Arm → Conveyor | - | `0x124` | `0x02` | DC 모터 벨트 시작 |
| Robot Arm → Linux | `D` | `0x125` | `0x01` | 로봇팔 작업 완료 |
| Robot Arm → Linux | `R` | `0x126` | `0x01` | 벨트 재동작 알림 |

STM32 수신 필터는 필요한 11비트 표준 ID만 FIFO0로 전달합니다. 표준 ID가 필터 레지스터 상위 비트에 배치되므로 5비트 왼쪽 이동해 설정했습니다.

~~~c
canFilter.FilterIdHigh = (CAN_CONTROL_ID << 5);
canFilter.FilterMaskIdHigh = (0x7FF << 5);
~~~

두 STM32의 Time Quantum 조합은 다르지만 최종 비트레이트는 같습니다.

`Baud Rate = APB1 Clock ÷ Prescaler ÷ (1 + BS1 + BS2)`

| 노드 | APB1 | Prescaler | BS1 | BS2 | 결과 |
|---|---:|---:|---:|---:|---:|
| Robot Arm | 45 MHz | 9 | 7 TQ | 2 TQ | 500 kbps |
| Conveyor | 45 MHz | 5 | 15 TQ | 2 TQ | 500 kbps |

구현 근거: [`stm_RobotArms/Core/Src/main.c`](./stm_RobotArms/Core/Src/main.c), [`CAN_Motor/Core/Src/main.c`](./CAN_Motor/Core/Src/main.c)

### 4. CAN ISR과 로봇팔 동작을 세마포어로 분리

로봇팔 동작은 수 초가 걸리므로 CAN 수신 콜백 안에서 직접 실행하면 다른 통신과 인터럽트 처리가 지연됩니다. 콜백은 세마포어만 release하고, 실제 동작은 `motorTask`가 수행하도록 분리했습니다.

~~~c
canSemaphoreHandle = osSemaphoreNew(1, 0, &canSemaphore_attributes);

if (osSemaphoreAcquire(canSemaphoreHandle, osWaitForever) == osOK)
{
    // Pick & Place 시퀀스 실행
}
~~~

세마포어 초기값을 `0`으로 두었기 때문에 부팅 직후 로봇팔이 임의로 동작하지 않습니다. 이벤트가 없을 때 태스크는 `osWaitForever`로 블로킹되어 CPU를 계속 점유하지 않습니다.

구현 근거: [`stm_RobotArms/Core/Src/freertos.c`](./stm_RobotArms/Core/Src/freertos.c)

### 5. 구동 장치별 FreeRTOS 태스크 분리

Conveyor Node는 하나의 긴 제어 함수 대신 구동부별 태스크를 사용합니다.

| 태스크 | 구동부 | 실행 조건과 동작 |
|---|---|---|
| `ConveyorTask` | 연속회전 서보 | 시스템 시작 시 회전하며 정지 상태 플래그 변화에 반응합니다. |
| `MotorBTask` | DC 모터 벨트 | `0x02` 명령을 받으면 PWM 70%로 3초간 동작합니다. |
| `MotorCTask` | 스텝모터 분류판 | `0xAC` 명령을 받으면 정회전·유지·역회전을 수행합니다. |

CAN ISR은 모터를 직접 움직이지 않고 volatile 명령 플래그만 변경합니다. 각 태스크가 실제 시퀀스를 실행하며, `Command`와 `Busy` 상태를 함께 확인해 동작 중 같은 명령이 다시 실행되는 것을 막습니다.

구현 근거: [`CAN_Motor/Core/Src/freertos.c`](./CAN_Motor/Core/Src/freertos.c)

---

## Motor control details

### 로봇팔과 그리퍼: 50 Hz Servo PWM

STM32 시스템 클럭 180 MHz, APB1 타이머 클럭 90 MHz를 기준으로 TIM2를 설정했습니다.

| 설정 | 값 | 의미 |
|---|---:|---|
| Prescaler | 89 | 90 MHz를 1 MHz로 분주, 1 tick = 1 µs |
| ARR | 19,999 | 20 ms 주기, 50 Hz |
| 펄스폭 | 500 / 1,500 / 2,500 µs | 0° / 90° / 180° 기준 |

로봇팔은 90° 대기 자세에서 시작해 다음 순서로 동작합니다.

`대기 90° → 접근 180° → 그리퍼 닫기 → 이송 0° → 그리퍼 열기 → 90° 복귀`

90°에서 180°로 이동하는 구간은 목표와 내부 추정 위치의 오차를 사용한 PD 형태의 보간을 적용했습니다. 엔코더가 없는 오픈 루프이므로 실제 위치 피드백 제어가 아니라, 급격한 펄스 변화와 기구 충격을 줄이기 위한 소프트웨어 궤적입니다.

### 메인 컨베이어: 연속회전 서보

- TIM2 CH1 사용
- 동작 펄스: 1,900 µs
- 정지 명령: PWM 출력 중단 후 신호 핀 Low
- 재가동 명령: PWM 출력과 설정 펄스 복구

### 보조 컨베이어: DC 모터 PWM

- TIM3 PWM 주파수: 1 kHz
- CCR 700 / ARR 999: 약 70% duty
- H-Bridge 방향 핀과 PWM을 함께 제어
- 작업 완료 후 3초간 동작하고 정지

### 분류판: 28BYJ-48 하프스텝

4개 GPIO로 8단계 하프스텝 시퀀스를 출력합니다. 4,096스텝을 한 회전으로 보아 512스텝으로 약 45° 이동한 뒤, 2초간 유지하고 역순 시퀀스로 복귀합니다.

~~~c
if (direction == MOTOR_DIR_FORWARD)
{
    stepIndex = i % 8;
}
else
{
    stepIndex = 7 - (i % 8);
}
~~~

---

## 문제를 어떻게 좁히고 해결했는가

| 문제 | 원인 | 적용한 해결 방식 |
|---|---|---|
| 같은 QR에서 모터 명령이 반복될 수 있음 | 카메라는 같은 물체를 여러 프레임에서 인식함 | Linux 상태 변수와 MCU `Command/Busy` 플래그로 중복 실행 방지 |
| CAN 콜백에서 긴 동작을 수행하면 통신이 지연됨 | 로봇팔 동작이 수 초 동안 실행됨 | ISR은 세마포어만 전달하고 FreeRTOS 태스크가 모터 시퀀스 수행 |
| 여러 모터의 동작 순서가 서로 얽힘 | 한 루프에서 모든 구동부를 순차 제어하려는 구조 | 컨베이어·DC 모터·스텝모터를 독립 태스크로 분리 |
| 서로 다른 CAN 설정값 때문에 통신 여부 판단이 어려움 | Prescaler와 BS 조합은 달라도 최종 Baud는 같을 수 있음 | APB1 45 MHz를 기준으로 두 노드 모두 500 kbps가 되도록 계산 |
| 전체 결합 후 오류 지점을 찾기 어려움 | 영상·통신·RTOS·모터가 동시에 연결됨 | `BaseCode`에 CAN 수신, 정지, 데이터 분기, 노드 통합 단계를 나누어 검증 |
| 서보가 목표 각도로 급격히 이동함 | CCR 값을 즉시 변경하면 기구 충격이 발생함 | 내부 위치를 조금씩 갱신하는 PD 형태의 궤적 적용 |

### 단계별 통합 기록

`BaseCode`에는 다음 순서의 검증 코드가 남아 있습니다.

1. CAN 송수신과 LED 반응 확인
2. `0x124` 수신 시 컨베이어 정지
3. 같은 CAN ID의 데이터 값으로 모터 동작 분기
4. 로봇팔과 컨베이어 사이의 완료·재가동 신호 통합
5. 속도 조절이 가능한 DC 모터 구동부로 변경

---

## 구현 확인 항목

| 확인 항목 | 결과 |
|---|---|
| QR 디코딩 | 외곽선, 문자열, 중심점을 영상에 표시합니다. |
| 위치 트리거 | 중심점이 판정선 ±20 px 범위에 들어오면 분류합니다. |
| Pass 처리 | `stop`, `trash` 이외 QR은 모터 명령 없이 통과합니다. |
| UART-CAN 변환 | `A`, `S`, `T`를 지정한 CAN ID와 데이터로 변환합니다. |
| CAN 수신 필터 | 각 STM32가 필요한 표준 ID만 FIFO0로 수신합니다. |
| 메인 컨베이어 | 부팅 시 구동하고 정지·재가동 명령에 반응합니다. |
| 로봇팔 | 픽업, 이송, 배치, 원위치 복귀를 수행합니다. |
| 분류판 | 512스텝 정회전, 2초 유지, 역회전 복귀를 수행합니다. |
| DC 모터 벨트 | 약 70% duty로 3초간 동작합니다. |
| 중복 명령 방지 | 모터 동작 중 같은 명령을 busy 상태로 무시합니다. |
| 완료 피드백 | CAN 완료 신호를 UART 상태 문자로 Linux에 전달합니다. |

> 위 항목은 저장소의 최종 소스와 첨부된 실행 자료를 기준으로 정리했습니다. 정량적인 인식률·처리 시간·반복 내구성은 아직 측정값이 없어 임의의 수치를 기재하지 않았습니다.

---

## 실행 준비

전체 시스템은 Linux 제어기, Arduino 게이트웨이, 두 대의 STM32 보드와 실제 구동부가 모두 연결되어야 동작합니다.

### 1. Linux 영상 처리 프로그램

~~~bash
sudo apt update
sudo apt install build-essential pkg-config libopencv-dev libzbar-dev
~~~

Arduino가 연결된 장치 경로를 환경에 맞게 확인합니다.

~~~cpp
uart_fd = init_uart("/dev/ttyACM0", B115200);
~~~

~~~bash
make
./qr_scanner
~~~

실행 중 `q`를 누르면 영상 스레드와 UART를 정리하고 종료합니다.

### 2. Arduino UART-CAN 게이트웨이

1. Arduino IDE에 MCP2515용 `mcp_can` 라이브러리를 설치합니다.
2. 코드의 `SPI_CS_PIN`과 실제 CS 핀을 맞춥니다.
3. MCP2515 모듈 오실레이터가 8 MHz인지 확인합니다.
4. CAN 500 kbps, UART 115200 bps 설정으로 `arduino.cpp`를 업로드합니다.

### 3. STM32 펌웨어

| 보드 | CubeMX 프로젝트 | 역할 |
|---|---|---|
| STM32 #1 | `stm_RobotArms/stm_RobotArms.ioc` | 로봇팔과 그리퍼 |
| STM32 #2 | `CAN_Motor/CAN_Motor.ioc` | 메인 벨트, 분류판, DC 모터 벨트 |

각 `.ioc` 파일을 STM32CubeIDE에서 열어 빌드한 뒤 NUCLEO-F446RE 보드에 업로드합니다.

### 4. 배선 시 주의 사항

- 모든 CAN 노드는 CAN-H, CAN-L, GND를 공통으로 연결합니다.
- 버스 양 끝에는 120 Ω 종단 저항을 적용합니다.
- STM32 CAN 주변장치에는 별도의 CAN 트랜시버가 필요합니다.
- 서보와 DC 모터는 외부 전원을 사용하고 MCU와 GND를 공통으로 연결합니다.
- 로봇팔의 실제 기구 한계에 맞게 500~2,500 µs 범위를 보정합니다.

---

## Repository map

~~~text
ProjectFolder
├─ QR_Detect.cpp                     # QR 인식, 위치 판정, UART 상태 관리
├─ arduino.cpp                       # UART-CAN 게이트웨이
├─ Makefile                          # Linux 프로그램 빌드
├─ CAN_Motor                         # 컨베이어 STM32 펌웨어
│  ├─ CAN_Motor.ioc
│  └─ Core/Src
│     ├─ main.c                      # CAN 필터와 명령 디코딩
│     └─ freertos.c                  # 구동부별 태스크와 모터 함수
├─ stm_RobotArms                     # 로봇팔 STM32 펌웨어
│  ├─ stm_RobotArms.ioc
│  └─ Core/Src
│     ├─ main.c                      # CAN ISR과 송신 함수
│     └─ freertos.c                  # 세마포어 기반 로봇팔 태스크
├─ BaseCode                          # 단계별 통합 기록
├─ Screenshot
│  ├─ SmartConveyor_Architecture.png # 시스템 아키텍처 이미지
│  ├─ QR인식사진.png
│  ├─ 로봇팔잡기사진.png
│  ├─ 전체실행영상.mp4
│  └─ 카메라QR영상.mp4
└─ README3.md
~~~

`Debug`, `Drivers`, `Middlewares`에는 STM32CubeIDE가 생성한 빌드 결과, HAL, FreeRTOS 의존 코드가 포함되어 있습니다.

---

## 다음 개선 목표

| 현재 구조 | 다음 단계 |
|---|---|
| QR이 판정선 근처에 여러 프레임 머물 수 있음 | QR 값과 시간 기반 debounce 또는 공정 상태 머신 추가 |
| 로봇팔 위치가 명령 펄스 기반 오픈 루프임 | 엔코더·리미트 스위치를 추가한 폐루프 위치 제어 |
| UART 포트와 명령 문자가 코드에 고정됨 | 실행 인자 또는 설정 파일로 통신 설정 분리 |
| CAN 송신 결과와 Bus-Off 복구가 부족함 | 반환값 확인, 재전송, 오류 카운터, Bus-Off 복구 추가 |
| 일부 태스크 전달에 공유 플래그를 사용함 | RTOS Message Queue 또는 Event Flags로 통일 |
| 통합 검증이 수동 실행 중심임 | CAN 로그와 모터 타이밍을 검증하는 HIL 테스트 구성 |
| 로봇팔 궤적이 내부 추정값에 의존함 | 가감속 프로파일과 관절별 캘리브레이션 테이블 적용 |

---

## 포트폴리오 제출 전 체크

- [ ] 팀 인원과 본인 담당 기능 작성
- [ ] 정확한 개발 시작일·종료일 작성
- [ ] 전체 실행 영상을 GitHub user-attachments 주소로 교체
- [ ] 본인이 해결한 가장 큰 문제를 한 문단으로 추가
- [ ] 가능하다면 QR 인식 성공률과 공정 처리 시간을 반복 측정해 기재
