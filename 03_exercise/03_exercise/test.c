// test.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

char write_data[] = "hai iam salman from user space";
char read_data[2000];
unsigned int kernel_buff_size = 0;
int i = 0;

int main(void)
{
    int fd = open("/dev/Blck_Device_Drv", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    while ( i < 10)
    {
    ssize_t wr = write(fd, write_data, strlen(write_data));
    
    if (wr <= 0) {
        printf("write");
        //close(fd);
        break;
        return 1;
    }
    printf("%d Successfully written\n",i);
    i++;
}

    ssize_t rd = read(fd, read_data, 2000);
    
    if (rd <= 0) {
        perror("read");
        close(fd);
        return 1;
    }

    printf(" The data got from Kernal space is %s\n", read_data);

    close(fd);
    return 0;
}