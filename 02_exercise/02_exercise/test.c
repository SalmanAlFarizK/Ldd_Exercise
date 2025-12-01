// test.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define FETCH_KERNEL_SIZE           _IOR('a', 1, int*)
#define CLEAR_KERNEL_BUFFER         _IOW('a', 2, int*)

char write_data[] = "hai iam salman from user space";
char read_data[100];
unsigned int kernel_buff_size = 0;;

int main(void)
{
    int fd = open("/dev/Logger_Device", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    ssize_t wr = write(fd, write_data, strlen(write_data));
    
    if (wr <= 0) {
        printf("write");
        close(fd);
        return 1;
    }
    printf("Successfully written\n");

    ssize_t rd = read(fd, read_data, 100);
    
    if (rd <= 0) {
        perror("read");
        close(fd);
        return 1;
    }

    printf("The data got from Kernal space is %s\n",read_data);
    ioctl(fd, FETCH_KERNEL_SIZE, &kernel_buff_size);

    printf("The size is %d\n",kernel_buff_size);

    close(fd);
    return 0;
}