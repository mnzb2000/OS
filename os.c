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
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>

#define MAX_DEVICES 64
#define DEVICE_DIR "/dev/input/"
#define SLEEP_MICROSECONDS 10000

// Shared counters (thread-safe)
atomic_int keyboard_count = 0;
atomic_int mouse_count = 0;
atomic_int scroll_value = 0; // will be updated with compare-and-swap

volatile sig_atomic_t stop_flag = 0; // set by signal handler

typedef struct {
    char path[512];
    bool is_mouse;
    bool is_keyboard;
} monitor_args_t;

void signal_handler(int sig) {
    // Async-signal-safe: just set a flag
    (void)sig;
    stop_flag = 1;
}

bool is_keyboard_device(const char *device_path) {
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) { close(fd); return false; }

    bool is_keyboard = libevdev_has_event_type(dev, EV_KEY) &&
        (libevdev_has_event_code(dev, EV_KEY, KEY_A) ||
         libevdev_has_event_code(dev, EV_KEY, KEY_SPACE) ||
         libevdev_has_event_code(dev, EV_KEY, KEY_ENTER));

    libevdev_free(dev);
    close(fd);
    return is_keyboard;
}

bool is_mouse_device(const char *device_path) {
    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) { close(fd); return false; }

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

// Thread worker: monitors a single device path
void *monitor_thread(void *arg) {
    monitor_args_t *ma = (monitor_args_t *)arg;
    int fd = open(ma->path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "[thread] Failed to open %s: %s\n", ma->path, strerror(errno));
        free(ma);
        return NULL;
    }

    struct libevdev *dev = NULL;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        fprintf(stderr, "[thread] libevdev init failed for %s: %s\n", ma->path, strerror(-rc));
        close(fd);
        free(ma);
        return NULL;
    }

    const char *name = libevdev_get_name(dev) ?: "(unknown)";
    printf("[thread] Monitoring %s -> %s (keyboard=%d mouse=%d)\n", ma->path, name, ma->is_keyboard, ma->is_mouse);

    while (!stop_flag) {
        struct input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            // Keyboard events
            if (ma->is_keyboard && ev.type == EV_KEY && ev.value == 1) {
                atomic_fetch_add(&keyboard_count, 1);
                printf("[kbd] %s key=%d total=%d\n", name, ev.code, atomic_load(&keyboard_count));
            }

            // Mouse buttons
            if (ma->is_mouse && ev.type == EV_KEY && ev.value == 1) {
                if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
                    atomic_fetch_add(&mouse_count, 1);
                    printf("[mouse] %s button=%s total=%d\n", name,
                        ev.code == BTN_LEFT ? "Left" : ev.code == BTN_RIGHT ? "Right" : "Middle",
                        atomic_load(&mouse_count));
                }
            }

            // Mouse scroll (relative)
            if (ma->is_mouse && ev.type == EV_REL && (ev.code == REL_WHEEL || ev.code == REL_HWHEEL)) {
                int oldv, newv;
                do {
                    oldv = atomic_load(&scroll_value);
                    long tmp = (long)oldv + (long)ev.value;
                    newv = (int)tmp;
                } while (!atomic_compare_exchange_weak(&scroll_value, &oldv, newv));

                printf("[scroll] %s delta=%d value=%d\n", name, ev.value, atomic_load(&scroll_value));
            }
        } else if (rc == -EAGAIN) {
            usleep(SLEEP_MICROSECONDS);
        } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // A sync status — try to handle or ignore; continue reading
            continue;
        } else {
            // Some other error or device removed
            fprintf(stderr, "[thread] Read error on %s: %s\n", ma->path, strerror(-rc));
            break;
        }
    }

    libevdev_free(dev);
    close(fd);
    free(ma);
    printf("[thread] Exiting monitor for %s\n", name);
    return NULL;
}

int main(void) {
    printf("=== Input Device Monitor (threaded) ===\n");
    printf("Run with sudo if you get permission errors. Press Ctrl+C to stop.\n\n");

    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    DIR *dir = opendir(DEVICE_DIR);
    if (!dir) {
        fprintf(stderr, "Failed to open %s: %s\n", DEVICE_DIR, strerror(errno));
        return EXIT_FAILURE;
    }

    struct dirent *entry;
    pthread_t threads[MAX_DEVICES];
    int tcount = 0;

    while ((entry = readdir(dir)) != NULL && tcount < MAX_DEVICES) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        char device_path[512];
        snprintf(device_path, sizeof(device_path), "%s%s", DEVICE_DIR, entry->d_name);

        bool k = is_keyboard_device(device_path);
        bool m = is_mouse_device(device_path);
        if (!k && !m) continue; // skip devices that are neither

        monitor_args_t *ma = malloc(sizeof(monitor_args_t));
        if (!ma) { fprintf(stderr, "Allocation failed\n"); break; }
        strncpy(ma->path, device_path, sizeof(ma->path));
        ma->path[sizeof(ma->path)-1] = '\0';
        ma->is_mouse = m;
        ma->is_keyboard = k;

        if (pthread_create(&threads[tcount], NULL, monitor_thread, ma) != 0) {
            fprintf(stderr, "Failed to create thread for %s\n", device_path);
            free(ma);
            continue;
        }
        tcount++;
    }
    closedir(dir);

    if (tcount == 0) {
        printf("No input devices found. Try: sudo ./input_monitor\n");
        return EXIT_FAILURE;
    }

    printf("Monitoring %d device(s).\n", tcount);

    // Wait for signal to stop
    while (!stop_flag) {
        pause(); // waiting for signal; threads do the work
    }

    // Wait for threads to exit
    for (int i = 0; i < tcount; ++i) pthread_join(threads[i], NULL);

    printf("\n=== Final counts ===\n");
    printf("Keyboard presses: %d\n", atomic_load(&keyboard_count));
    printf("Mouse clicks:      %d\n", atomic_load(&mouse_count));
    printf("Scroll value:      %d\n", atomic_load(&scroll_value));

    return EXIT_SUCCESS;
}
