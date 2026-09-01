// Vulkan or OpenGL visuals of a cube will be applied with the values from
// the frames.

  #include <fcntl.h>
  #include <termios.h>
  #include <unistd.h>
  #include <cstdio>
  #include <cstdint>

  int openSerialPort(const char *path) {
      int fd = open(path, O_RDONLY | O_NOCTTY);
      if (fd < 0) {
          perror("open");
          return -1;
      }

      termios tty{};
      tcgetattr(fd, &tty);          // read current settings
      cfsetispeed(&tty, B115200);   // set baud rate to match firmware
      tty.c_cflag |= (CLOCAL | CREAD);
      tty.c_cflag &= ~PARENB;       // no parity
      tty.c_cflag &= ~CSTOPB;       // 1 stop bit
      tty.c_cflag &= ~CSIZE;
      tty.c_cflag |= CS8;           // 8 data bits
      tty.c_lflag = 0;              // raw mode: no canonical/echo/signals
      tty.c_iflag = 0;
      tty.c_oflag = 0;
      tty.c_cc[VMIN] #include <fcntl.h>
}

int main() {
	int fd = openSerialPort("/dev/ttyUSB0");
	if (fd < 0){
		return 1;
	}


	return 0;
}
