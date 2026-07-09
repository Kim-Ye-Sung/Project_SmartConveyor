#include <opencv2/opencv.hpp>
#include <zbar.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

using namespace std;
using namespace cv;
using namespace zbar;

// 전역 변수 및 공유 자원
Mat shared_frame;
mutex frame_mtx;
atomic<bool> is_running(true);
atomic<bool> arm_is_run(false);
atomic<bool> is_belt_run(true);
int uart_fd = -1; // UART 파일 디스크립터

// UART 초기화 함수
int init_uart(const char *portname, int baudrate)
{
    int fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC | O_NONBLOCK);
    if (fd < 0)
    {
        perror("Error opening serial port");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0)
    {
        perror("Error from tcgetattr");
        return -1;
    }

    cfsetospeed(&tty, baudrate);
    cfsetispeed(&tty, baudrate);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; // 8-bit characters
    tty.c_iflag &= ~IGNBRK;                     // disable break processing
    tty.c_lflag = 0;                            // no signaling chars, no echo, no canonical processing
    tty.c_oflag = 0;                            // no remapping, no delays
    tty.c_cc[VMIN] = 0;                         // read doesn't block
    tty.c_cc[VTIME] = 1;                        // 0.1 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
    tty.c_cflag |= (CLOCAL | CREAD);        // ignore modem controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("Error from tcsetattr");
        return -1;
    }
    return fd;
}

// 카메라 입력 및 qr 계산 스레드
void vision_thread_func()
{
    VideoCapture cap(0);
    if (!cap.isOpened())
    {
        cerr << "camera error" << endl;
        is_running = false;
        return;
    }
    cap.set(CAP_PROP_FRAME_WIDTH, 640);
    cap.set(CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(CAP_PROP_AUTO_EXPOSURE, 0.25); // 수동 노출 모드
    cap.set(CAP_PROP_EXPOSURE, -5);
    ImageScanner scanner;
    scanner.set_config(ZBAR_NONE, ZBAR_CFG_ENABLE, 1);
    scanner.set_config(ZBAR_NONE, ZBAR_CFG_X_DENSITY, 1);
    scanner.set_config(ZBAR_NONE, ZBAR_CFG_Y_DENSITY, 1);

    Mat local_frame, gray;

    while (is_running)
    {
        cap >> local_frame;
        if (local_frame.empty())
            continue;

        cvtColor(local_frame, gray, COLOR_BGR2GRAY);
        int width = gray.cols;
        int height = gray.rows;
        zbar::Image zbar_img(width, height, "Y800", gray.data, width * height);

        scanner.scan(zbar_img);

        int line_y = static_cast<int>(height * (2.0 / 3.0));
        line(local_frame, Point(0, line_y), Point(width, line_y), Scalar(0, 0, 255), 2);

        for (Image::SymbolIterator symbol = zbar_img.symbol_begin(); symbol != zbar_img.symbol_end(); ++symbol)
        {
            string data = symbol->get_data();

            if (symbol->get_location_size() == 4)
            {
                vector<Point> vp;
                for (int i = 0; i < 4; i++)
                {
                    vp.push_back(Point(symbol->get_location_x(i), symbol->get_location_y(i)));
                }

                polylines(local_frame, vp, true, Scalar(0, 255, 0), 2);
                putText(local_frame, data, vp[0], FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 255, 0), 2);

                int centerX = (vp[0].x + vp[1].x + vp[2].x + vp[3].x) / 4;
                int centerY = (vp[0].y + vp[1].y + vp[2].y + vp[3].y) / 4;
                circle(local_frame, Point(centerX, centerY), 4, Scalar(0, 0, 255), -1);

                if (abs(centerY - line_y) < 20)
                {
                    if (data == "stop")
                    {
                        if (!arm_is_run)
                        {
                            cout << "arm_run" << endl;
                            char cmd = 'A';
                            write(uart_fd, &cmd, 1);
                            arm_is_run = true;
                            usleep(100000);
                        }
                        if (is_belt_run)
                        {
                            cout << "belt_stop" << endl;
                            char cmd = 'S';
                            write(uart_fd, &cmd, 1);
                            is_belt_run = false;
                            usleep(100000);
                        }
                    }
                    else if (data == "trash")
                    {
                        cout << "trash" << endl;
                        char cmd = 'T';
                        write(uart_fd, &cmd, 1);
                    }
                    else
                    {
                        cout << "pass" << endl;
                    }
                }
            }
        }

        {
            lock_guard<mutex> lock(frame_mtx);
            local_frame.copyTo(shared_frame);
        }
    }
    cap.release();
}

int main()
{
    // UART 포트는 아두이노가 연결된 포트로 맞춰주세요. (보통 /dev/ttyACM0 또는 /dev/ttyUSB0)
    uart_fd = init_uart("/dev/ttyACM0", B115200);
    if (uart_fd < 0)
        return -1;

    thread vision_worker(vision_thread_func);

    Mat display_frame;
    char read_buf;

    while (is_running)
    {
        {
            lock_guard<mutex> lock(frame_mtx);
            if (!shared_frame.empty())
            {
                shared_frame.copyTo(display_frame);
            }
        }

        if (!display_frame.empty())
        {
            imshow("Multi-threaded Conveyor QR Scanner", display_frame);
        }

        if (waitKey(10) == 'q')
        {
            is_running = false;
            break;
        }

        // 아두이노로부터 UART 수신 대기 (Non-blocking)
        int nbytes = read(uart_fd, &read_buf, 1);
        if (nbytes > 0)
        {
            if (read_buf == 'D')
            {
                std::cout << "아두이노 수신: 로봇팔 동작 완료 신호 (0x125)" << std::endl;
                arm_is_run = false;
            }
            else if (read_buf == 'R')
            {
                std::cout << "아두이노 수신: 벨트 재동작 신호 (0x126)" << std::endl;
                is_belt_run = true;
            }
        }
        else if (nbytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror("Read error");
            break;
        }
    }

    if (vision_worker.joinable())
    {
        vision_worker.join();
    }

    destroyAllWindows();
    close(uart_fd);
    cout << "exit program" << endl;

    return 0;
}
