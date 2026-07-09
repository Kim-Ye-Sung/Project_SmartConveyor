# 컴파일러 설정
CXX = g++

# 컴파일 옵션 (-O3로 최적화 활성화, C++17 표준 사용)
CXXFLAGS = -O3 -std=c++17

# 타겟 실행 파일 이름
TARGET = qr_scanner

# 소스 파일
SRCS = QR_Detect.cpp

# pkg-config를 이용해 OpenCV 헤더 및 라이브러리 경로 자동 지정
OPENCV_FLAGS = `pkg-config --cflags --libs opencv4`

# 링크할 라이브러리 (ZBar 라이브러리 추가)
LIBS = -lzbar

# 기본 빌드 규칙
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(OPENCV_FLAGS) $(LIBS)

# 빌드 파일 정리 (Clean)
clean:
	rm -f $(TARGET)

.PHONY: all clean