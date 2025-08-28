#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <csignal>
#include <memory>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <chrono>

constexpr int MAX_DEVICES = 64;
constexpr const char* DEVICE_DIR = "/dev/input/";
constexpr useconds_t SLEEP_MICROSECONDS = 10000;
constexpr int DASHBOARD_INTERVAL = 5; // seconds

// Shared counters
std::atomic<int> keyboard_count{0};
std::atomic<int> mouse_count{0};
std::atomic<int> scroll_count{0};
double mouse_distance = 0.0;
std::mutex mouse_distance_mutex;

// Stop flag
volatile sig_atomic_t stop_flag = 0;

// Timer
std::chrono::steady_clock::time_point start_time;

// Signal handler
void signal_handler(int sig) {
    (void)sig;
    stop_flag = 1;
}

struct MonitorArgs {
    std::string path;
    bool is_mouse{false};
    bool is_keyboard{false};
};

// Detect if device is a keyboard
bool is_keyboard_device(const std::string& device_path) {
    int fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    libevdev* dev = nullptr;
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

// Detect if device is a mouse
bool is_mouse_device(const std::string& device_path) {
    int fd = open(device_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    libevdev* dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) { close(fd); return false; }

    bool has_mouse_buttons = libevdev_has_event_type(dev, EV_KEY) &&
        (libevdev_has_event_code(dev, EV_KEY, BTN_LEFT) ||
         libevdev_has_event_code(dev, EV_KEY, BTN_RIGHT) ||
         libevdev_has_event_code(dev, EV_KEY, BTN_MIDDLE));

    bool has_scroll = libevdev_has_event_type(dev, EV_REL) &&
        libevdev_has_event_code(dev, EV_REL, REL_WHEEL);

    bool has_motion = libevdev_has_event_type(dev, EV_REL) &&
        (libevdev_has_event_code(dev, EV_REL, REL_X) ||
         libevdev_has_event_code(dev, EV_REL, REL_Y));

    libevdev_free(dev);
    close(fd);
    return has_mouse_buttons || has_scroll || has_motion;
}

// Thread to monitor a single device
void monitor_device(std::unique_ptr<MonitorArgs> ma) {
    int fd = open(ma->path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "[thread] Failed to open " << ma->path << ": " << strerror(errno) << "\n";
        return;
    }

    libevdev* dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        std::cerr << "[thread] libevdev init failed for " << ma->path << ": " << strerror(-rc) << "\n";
        close(fd);
        return;
    }

    const char* name = libevdev_get_name(dev) ? libevdev_get_name(dev) : "(unknown)";
    std::cout << "[thread] Monitoring " << ma->path << " -> " << name
              << " (keyboard=" << ma->is_keyboard << " mouse=" << ma->is_mouse << ")\n";

    while (!stop_flag) {
        input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            // Keyboard events
            if (ma->is_keyboard && ev.type == EV_KEY && ev.value == 1) {
                keyboard_count.fetch_add(1);
            }

            // Mouse button events
            if (ma->is_mouse && ev.type == EV_KEY && ev.value == 1) {
                if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
                    mouse_count.fetch_add(1);
                }
            }

            // Scroll events (absolute magnitude)
            if (ma->is_mouse && ev.type == EV_REL && (ev.code == REL_WHEEL || ev.code == REL_HWHEEL)) {
                int oldv, newv;
                do {
                    oldv = scroll_count.load();
                    long tmp = static_cast<long>(oldv) + std::abs(static_cast<long>(ev.value));
                    newv = static_cast<int>(tmp);
                } while (!scroll_count.compare_exchange_weak(oldv, newv));
            }

            // Mouse motion (distance)
            if (ma->is_mouse && ev.type == EV_REL && (ev.code == REL_X || ev.code == REL_Y)) {
                double delta = std::sqrt(
                    (ev.code == REL_X ? ev.value : 0) * (ev.code == REL_X ? ev.value : 0) +
                    (ev.code == REL_Y ? ev.value : 0) * (ev.code == REL_Y ? ev.value : 0)
                );
                std::lock_guard<std::mutex> lock(mouse_distance_mutex);
                mouse_distance += delta;
            }
        } else if (rc == -EAGAIN) {
            usleep(SLEEP_MICROSECONDS);
        } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            continue;
        } else if (rc != LIBEVDEV_READ_STATUS_SUCCESS) {
            std::cerr << "[thread] Read error on " << ma->path << ": " << strerror(-rc) << "\n";
            break;
        }
    }

    libevdev_free(dev);
    close(fd);
    std::cout << "[thread] Exiting monitor for " << name << "\n";
}

// Dashboard thread
void dashboard_thread() {
    while (!stop_flag) {
        sleep(DASHBOARD_INTERVAL);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        double distance_copy;
        {
            std::lock_guard<std::mutex> lock(mouse_distance_mutex);
            distance_copy = mouse_distance;
        }

        std::cout << "\n=== Dashboard ===\n";
        std::cout << "Keyboard presses: " << keyboard_count.load() << "\n";
        std::cout << "Mouse clicks:     " << mouse_count.load() << "\n";
        std::cout << "Scroll magnitude: " << scroll_count.load() << "\n";
        std::cout << "Mouse distance:   " << distance_copy << "\n";
        std::cout << "Elapsed time:     " << elapsed << " seconds\n";
        std::cout << "================\n";
    }
}

int main() {
    std::cout << "=== Input Device Monitor (C++ threaded) ===\n";
    std::cout << "Run with sudo if you get permission errors. Press Ctrl+C to stop.\n";

    // Signal handlers
    struct sigaction sa {};
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    DIR* dir = opendir(DEVICE_DIR);
    if (!dir) {
        std::cerr << "Failed to open " << DEVICE_DIR << ": " << strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    std::vector<std::thread> threads;
    dirent* entry;
    while ((entry = readdir(dir)) != nullptr && threads.size() < MAX_DEVICES) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        std::string device_path = std::string(DEVICE_DIR) + entry->d_name;
        bool k = is_keyboard_device(device_path);
        bool m = is_mouse_device(device_path);
        if (!k && !m) continue;

        auto ma = std::make_unique<MonitorArgs>();
        ma->path = device_path;
        ma->is_keyboard = k;
        ma->is_mouse = m;

        threads.emplace_back(monitor_device, std::move(ma));
    }
    closedir(dir);

    if (threads.empty()) {
        std::cout << "No input devices found. Try: sudo ./z\n";
        return EXIT_FAILURE;
    }

    std::cout << "Monitoring " << threads.size() << " device(s).\n";

    // Start timer
    start_time = std::chrono::steady_clock::now();

    // Start dashboard
    std::thread dash_thread(dashboard_thread);

    // Wait for threads to exit
    for (auto& t : threads) t.join();
    stop_flag = 1;
    dash_thread.join();

    std::cout << "\n=== Final counts ===\n";
    std::cout << "Keyboard presses: " << keyboard_count.load() << "\n";
    std::cout << "Mouse clicks:     " << mouse_count.load() << "\n";
    std::cout << "Scroll magnitude: " << scroll_count.load() << "\n";
    std::cout << "Mouse distance:   " << mouse_distance << "\n";
    auto final_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start_time).count();
    std::cout << "Elapsed time:     " << final_elapsed << " seconds\n";

    return 0;
}
