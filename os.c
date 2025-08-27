#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

#define MAX_DEVICES 20
#define DEVICE_DIR "/dev/input/"
#define SLEEP_MICROSECONDS 10000
#define SCROLL_LIMIT 100

// Global counters
volatile sig_atomic_t keyboard_count = 0;
volatile sig_atomic_t mouse_count = 0;
volatile sig_atomic_t scroll_value = 0;

// Function declarations
bool is_keyboard_device(const char *device_path);
bool is_mouse_device(const char *device_path);
void monitor_device(const char *device_path, bool is_mouse);
void signal_handler(int sig);

// Check if device is a keyboard
bool is_keyboard_device(const char *device_path) {
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        close(fd);
        return false;
    }

    // Check if device has keyboard capabilities
    bool is_keyboard = libevdev_has_event_type(dev, EV_KEY) && 
                      (libevdev_has_event_code(dev, EV_KEY, KEY_A) ||
                       libevdev_has_event_code(dev, EV_KEY, KEY_SPACE) ||
                       libevdev_has_event_code(dev, EV_KEY, KEY_ENTER));

    libevdev_free(dev);
    close(fd);

    return is_keyboard;
}

// Check if device is a mouse
bool is_mouse_device(const char *device_path) {
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        close(fd);
        return false;
    }

    // Check if device has mouse capabilities
    bool has_mouse_buttons = libevdev_has_event_type(dev, EV_KEY) && 
                   (libevdev_has_event_code(dev, EV_KEY, BTN_LEFT) ||
                    libevdev_has_event_code(dev, EV_KEY, BTN_RIGHT) ||
                    libevdev_has_event_code(dev, EV_KEY, BTN_MIDDLE));
    
    bool has_scroll = libevdev_has_event_type(dev, EV_REL) &&
                     libevdev_has_event_code(dev, EV_REL, REL_WHEEL);

    libevdev_free(dev);
    close(fd);

    return has_mouse_buttons || has_scroll;
}

// Monitor a single input device
void monitor_device(const char *device_path, bool is_mouse) {
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device %s: %s\n", device_path, strerror(errno));
        return;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "Failed to initialize libevdev for %s: %s\n", device_path, strerror(-rc));
        close(fd);
        return;
    }

    printf("Monitoring: %s (%s) - %s\n", libevdev_get_name(dev), device_path, is_mouse ? "Mouse" : "Keyboard");

    // Main monitoring loop
    while (1) {
        struct input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (is_mouse) {
                // Mouse button press
                if (ev.type == EV_KEY && ev.value == 1) {
                    if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
                        __sync_fetch_and_add(&mouse_count, 1);
                        printf("Mouse button pressed! Total mouse clicks: %d\n", mouse_count);
                        printf("  Button: %s\n", ev.code == BTN_LEFT ? "Left" : ev.code == BTN_RIGHT ? "Right" : "Middle");
                        printf("  Device: %s\n", libevdev_get_name(dev));
                    }
                }
                // Mouse scroll
                else if (ev.type == EV_REL && (ev.code == REL_WHEEL || ev.code == REL_HWHEEL)) {
                    int new_scroll = scroll_value + ev.value;
                    
                    // Apply scroll limits (-100 to +100)
                    if (new_scroll > SCROLL_LIMIT) new_scroll = SCROLL_LIMIT;
                    if (new_scroll < -SCROLL_LIMIT) new_scroll = -SCROLL_LIMIT;
                    
                    scroll_value = new_scroll;
                    printf("Mouse scrolled! Current scroll value: %d\n", scroll_value);
                    printf("  Direction: %s\n", ev.value > 0 ? "Up/Right" : "Down/Left");
                    printf("  Device: %s\n", libevdev_get_name(dev));
                }
            } else {
                // Keyboard key press
                if (ev.type == EV_KEY && ev.value == 1) {
                    __sync_fetch_and_add(&keyboard_count, 1);
                    printf("Keyboard key pressed! Total key presses: %d\n", keyboard_count);
                    printf("  Key code: %d\n", ev.code);
                    printf("  Device: %s\n", libevdev_get_name(dev));
                }
            }
        } else if (rc == -EAGAIN) {
            // No data available, sleep briefly
            usleep(SLEEP_MICROSECONDS);
        } else {
            // Error occurred
            fprintf(stderr, "Error reading from device %s: %s\n", device_path, strerror(-rc));
            break;
        }
    }

    // Cleanup
    libevdev_free(dev);
    close(fd);
}

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("\nReceived signal %d. Exiting...\n", sig);
    printf("Final counts:\n");
    printf("  Keyboard presses: %d\n", keyboard_count);
    printf("  Mouse clicks: %d\n", mouse_count);
    printf("  Scroll value: %d\n", scroll_value);
    exit(EXIT_SUCCESS);
}

int main() {
    printf("=== Input Device Monitor ===\n");
    printf("Features:\n");
    printf("  1. Separate keyboard and mouse counters\n");
    printf("  2. Scroll tracking (-100 to +100)\n");
    printf("  3. Support for multiple keyboards and mice\n");
    printf("Press Ctrl+C to exit\n\n");

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    DIR *dir = opendir(DEVICE_DIR);
    if (!dir) {
        fprintf(stderr, "Failed to open device directory %s: %s\n", DEVICE_DIR, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("Scanning for input devices in %s...\n", DEVICE_DIR);

    struct dirent *entry;
    char device_path[512];
    pid_t pids[MAX_DEVICES];
    int device_count = 0;

    // Scan for input devices
    while ((entry = readdir(dir)) != NULL && device_count < MAX_DEVICES) {
        if (strncmp(entry->d_name, "event", 5) == 0) {
            snprintf(device_path, sizeof(device_path), "%s%s", DEVICE_DIR, entry->d_name);
            
            // Check if it's a keyboard
            if (is_keyboard_device(device_path)) {
                printf("Found keyboard: %s\n", device_path);
                pid_t pid = fork();
                
                if (pid == 0) { // Child process
                    monitor_device(device_path, false);
                    exit(EXIT_SUCCESS);
                } else if (pid > 0) { // Parent process
                    pids[device_count++] = pid;
                } else {
                    fprintf(stderr, "Fork failed for device %s: %s\n", device_path, strerror(errno));
                }
            }
            // Check if it's a mouse
            else if (is_mouse_device(device_path)) {
                printf("Found mouse: %s\n", device_path);
                pid_t pid = fork();
                
                if (pid == 0) { // Child process
                    monitor_device(device_path, true);
                    exit(EXIT_SUCCESS);
                } else if (pid > 0) { // Parent process
                    pids[device_count++] = pid;
                } else {
                    fprintf(stderr, "Fork failed for device %s: %s\n", device_path, strerror(errno));
                }
            }
        }
    }
    closedir(dir);

    if (device_count == 0) {
        printf("No suitable input devices found!\n");
        printf("Try running with sudo: sudo ./input_monitor\n");
        printf("Available devices:\n");
        system("ls -la /dev/input/ | grep event");
        return EXIT_FAILURE;
    }

    printf("\n=== Monitoring %d input devices ===\n", device_count);
    printf("Initial scroll value: %d\n", scroll_value);
    printf("Start using your input devices...\n");
    printf("=======================================\n");

    // Wait for all child processes
    int status;
    for (int i = 0; i < device_count; i++) {
        waitpid(pids[i], &status, 0);
    }

    printf("\nFinal counts:\n");
    printf("  Keyboard presses: %d\n", keyboard_count);
    printf("  Mouse clicks: %d\n", mouse_count);
    printf("  Scroll value: %d\n", scroll_value);
    
    return EXIT_SUCCESS;
}