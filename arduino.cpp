#include <SPI.h>
#include <mcp_can.h>

const int SPI_CS_PIN = 10; // CAN 모듈의 CS 핀에 맞게 수정
MCP_CAN CAN(SPI_CS_PIN);

void setup()
{
  Serial.begin(115200); // PC와 동일한 보드레이트 설정

  // CAN 통신 초기화 (속도는 장치에 맞게 500KBPS 등으로 설정)
  while (CAN_OK != CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ))
  {
    Serial.println("CAN Init Failed");
    delay(100);
  }
  Serial.println("CAN Init Success!");
  CAN.setMode(MCP_NORMAL); // 초기화 후 반드시 정상(Normal) 통신 모드로 변경해주어야 합니다.
}

void loop()
{
  // 1. PC(C++)로부터 UART 명령 수신 -> CAN 송신
  if (Serial.available() > 0)
  {
    char cmd = Serial.read();
    unsigned char stmp[1] = {0};

    if (cmd == 'S')
    {
      stmp[0] = 0xAA;
      CAN.sendMsgBuf(0x124, 0, 1, stmp);
      delay(2);
    }
    else if (cmd == 'A')
    {
      stmp[0] = 0xAB;
      CAN.sendMsgBuf(0x123, 0, 1, stmp);
      delay(2);
    }
    else if (cmd == 'T')
    {
      stmp[0] = 0xAC;
      CAN.sendMsgBuf(0x124, 0, 1, stmp);
      delay(2);
    }
  }

  // 2. 외부 CAN 모듈로부터 메시지 수신 -> PC(C++)로 UART 송신
  if (CAN_MSGAVAIL == CAN.checkReceive())
  {
    long unsigned int rxId;
    unsigned char len = 0;
    unsigned char rxBuf[8];

    CAN.readMsgBuf(&rxId, &len, rxBuf);

    if (rxId == 0x125)
    {
      Serial.write('D'); // 로봇팔 완료
    }
    else if (rxId == 0x126)
    {
      Serial.write('R'); // 벨트 재동작
    }
  }
}
