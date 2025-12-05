// test.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define TURN_ON_LED                                     _IOW('a', 1, int*)
#define TURN_OFF_LED                                    _IOW('a', 2, int*)
#define SET_TEMPERATURE                                 _IOW('a', 3, int*)
#define SET_BRIGHTNESS                                  _IOW('a', 4, int*)
#define GET_CURRENT_LED_STATE                           _IOR('a', 5, int*)

struct light_state
{
    int brightness;
    int temperature;
    int is_on;
    int active_users;
};

struct light_state light_state_status;
int temp = 2500;
int brightness = 17;

int main(void)
{
    int fd = open("/dev/Smart_light_controller_device", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }


    ioctl(fd, TURN_ON_LED, NULL);
    ioctl(fd, SET_TEMPERATURE, &temp);
    ioctl(fd, SET_BRIGHTNESS, &brightness);

    ioctl(fd, GET_CURRENT_LED_STATE, (int*)&light_state_status);

    printf("Led brightness: %d\n",light_state_status.brightness);
    printf("Led temperature: %d\n",light_state_status.temperature);
    printf("Led is on: %d\n",light_state_status.is_on);
    printf("Led users: %d\n",light_state_status.active_users);

    close(fd);
    return 0;
}