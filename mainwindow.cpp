#include "mainwindow.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QMessageBox>
#include <QDir>
#include <QFrame>
#include <QIcon>
#include <QMessageBox>
#include <QDebug>
#include <QFile>

#include <iostream>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <libevdev/libevdev.h>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>

constexpr const char* DEVICE_DIR = "/dev/input/";
constexpr useconds_t SLEEP_MICROSECONDS = 10000;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
    monitoring(false)
{
    // Set window properties
    setWindowTitle("KnM_Tracker");
    setFixedSize(800, 500);

    // Main central widget
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // Main layout for the central widget (will be a grid for left and right panels)
    QGridLayout *mainLayout = new QGridLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Left Panel (Statistics) ---
    QFrame *leftPanel = new QFrame(centralWidget);
    leftPanel->setStyleSheet("background-color: white;");
    mainLayout->addWidget(leftPanel, 0, 0, 1, 1);

    QVBoxLayout *leftPanelLayout = new QVBoxLayout(leftPanel);
    leftPanelLayout->setAlignment(Qt::AlignCenter);
    leftPanelLayout->setSpacing(20);

    // Keyboard Press
    QLabel *keyboardLabel = new QLabel("Keyboard Press", this);
    keyboardLabel->setAlignment(Qt::AlignCenter);
    keyboardLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(keyboardLabel);

    keyboardCountLabel = new QLabel("0", this);
    keyboardCountLabel->setAlignment(Qt::AlignCenter);
    keyboardCountLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(keyboardCountLabel);

    // Mouse Click
    QLabel *mouseClickLabel = new QLabel("Mouse Click", this);
    mouseClickLabel->setAlignment(Qt::AlignCenter);
    mouseClickLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(mouseClickLabel);

    mouseCountLabel = new QLabel("0", this);
    mouseCountLabel->setAlignment(Qt::AlignCenter);
    mouseCountLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(mouseCountLabel);

    // Mouse Scroll
    QLabel *mouseScrollLabel = new QLabel("Mouse Scroll", this);
    mouseScrollLabel->setAlignment(Qt::AlignCenter);
    mouseScrollLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(mouseScrollLabel);

    scrollCountLabel = new QLabel("0", this);
    scrollCountLabel->setAlignment(Qt::AlignCenter);
    scrollCountLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(scrollCountLabel);

    // Mouse Movement
    QLabel *mouseMovementLabel = new QLabel("Mouse Movement", this);
    mouseMovementLabel->setAlignment(Qt::AlignCenter);
    mouseMovementLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(mouseMovementLabel);

    mouseDistanceLabel = new QLabel("0", this);
    mouseDistanceLabel->setAlignment(Qt::AlignCenter);
    mouseDistanceLabel->setStyleSheet("font-size: 36px; font-weight: bold; color: black;");
    leftPanelLayout->addWidget(mouseDistanceLabel);

    // --- Right Panel (Timer and Controls) ---
    QFrame *rightPanel = new QFrame(centralWidget);
    rightPanel->setStyleSheet("background-color: #E0E0E0;"); // Light grey background
    mainLayout->addWidget(rightPanel, 0, 1, 1, 1);

    QVBoxLayout *rightPanelLayout = new QVBoxLayout(rightPanel);
    rightPanelLayout->setAlignment(Qt::AlignCenter);
    rightPanelLayout->setSpacing(50);

    // Elapsed Time Label
    elapsedTimeLabel = new QLabel("00 : 00 : 00", this);
    elapsedTimeLabel->setAlignment(Qt::AlignCenter);
    elapsedTimeLabel->setStyleSheet("font-size: 60px; font-weight: bold; color: black;");
    rightPanelLayout->addWidget(elapsedTimeLabel);

    // Toggle Button
    toggleMonitoringButton = new QPushButton("Start", this);
    toggleMonitoringButton->setFixedSize(160, 60);
    toggleMonitoringButton->setStyleSheet(
        "QPushButton {"
        "background-color: #2ecc71;" // Green background for Start
        "color: white;"
        "border-radius: 6px;"
        "font-size: 20px;"
        "font-weight: bold;"
        "}"
        "QPushButton:pressed {"
        "background-color: #27ae60;" // Darker green when pressed
        "}"
        );
    connect(toggleMonitoringButton, &QPushButton::clicked, this, &MainWindow::onToggleMonitoring);
    rightPanelLayout->addWidget(toggleMonitoringButton, 0, Qt::AlignHCenter);rightPanelLayout->addWidget(toggleMonitoringButton, 0, Qt::AlignHCenter);





    // Set up update timer (updates UI every second)
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::updateDashboard);
}

MainWindow::~MainWindow()
{
    stopMonitoring();
}

void MainWindow::onToggleMonitoring()
{
    if (monitoring.load()) {
        std::cout << "Stopping monitoring...\n";
        stopMonitoring();
        toggleMonitoringButton->setText("Start");
        toggleMonitoringButton->setStyleSheet(
            "QPushButton {"
            "background-color: #2ecc71;"
            "color: white;"
            "border-radius: 6px;"
            "font-size: 20px;"
            "font-weight: bold;"
            "}"
            "QPushButton:pressed {"
            "background-color: #27ae60;"
            "}"
            );
    } else {
        std::cout << "Starting monitoring...\n";
        startMonitoring();
        toggleMonitoringButton->setText("Stop");
        toggleMonitoringButton->setStyleSheet(
            "QPushButton {"
            "background-color: #e74c3c;" // Red background for Stop
            "color: white;"
            "border-radius: 6px;"
            "font-size: 20px;"
            "font-weight: bold;"
            "}"
            "QPushButton:pressed {"
            "background-color: #c0392b;" // Darker red when pressed
            "}"
            );
    }
}

void MainWindow::updateDashboard()
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

    double distance_copy;
    {
        std::lock_guard<std::mutex> lock(mouse_distance_mutex);
        distance_copy = mouse_distance;
    }

    keyboardCountLabel->setText(QString::number(keyboard_count.load()));
    mouseCountLabel->setText(QString::number(mouse_count.load()));
    scrollCountLabel->setText(QString::number(scroll_count.load()));
    mouseDistanceLabel->setText(QString::number(static_cast<int>(distance_copy)));

    long long hours = elapsed / 3600;
    long long minutes = (elapsed % 3600) / 60;
    long long seconds = elapsed % 60;
    elapsedTimeLabel->setText(QString("%1 : %2 : %3")
                                  .arg(hours, 2, 10, QChar('0'))
                                  .arg(minutes, 2, 10, QChar('0'))
                                  .arg(seconds, 2, 10, QChar('0')));
}

void MainWindow::startMonitoring()
{
    std::cout << "=== Starting Input Device Monitor ===\n";

    // Reset counters
    keyboard_count = 0;
    mouse_count = 0;
    scroll_count = 0;
    mouse_distance = 0.0;

    // Set monitoring flag BEFORE starting threads
    monitoring = true;
    std::cout << "Monitoring flag set to: " << monitoring.load() << "\n";

    QDir inputDir(DEVICE_DIR);
    QStringList filters;
    filters << "event*";
    QStringList eventFiles = inputDir.entryList(filters, QDir::System | QDir::Files);

    if (eventFiles.isEmpty()) {
        std::cout << "No input devices found. Try running with sudo.\n";
        monitoring = false;
        QMessageBox::warning(this, "Warning", "No input devices found. Try running with sudo.");
        toggleMonitoringButton->setText("Start");
        toggleMonitoringButton->setStyleSheet(
            "QPushButton {"
            "background-color: #2ecc71;"
            "color: white;"
            "border-radius: 6px;"
            "font-size: 20px;"
            "font-weight: bold;"
            "}"
            "QPushButton:pressed {"
            "background-color: #27ae60;"
            "}"
            );
        return;
    }

    int device_count = 0;
    for (const QString &file : eventFiles) {
        QString devicePath = inputDir.absoluteFilePath(file);
        if (isKeyboardDevice(devicePath)) {
            std::cout << "Starting thread for keyboard device: " << devicePath.toStdString() << "\n";
            monitorThreads.emplace_back(&MainWindow::monitorDevice, this, devicePath.toStdString(), DeviceType::Keyboard);
            device_count++;
        } else if (isMouseDevice(devicePath)) {
            std::cout << "Starting thread for mouse device: " << devicePath.toStdString() << "\n";
            monitorThreads.emplace_back(&MainWindow::monitorDevice, this, devicePath.toStdString(), DeviceType::Mouse);
            device_count++;
        }
    }

    if (monitorThreads.empty()) {
        std::cout << "No input devices found. Try running with sudo.\n";
        monitoring = false;
        QMessageBox::warning(this, "Warning", "No input devices found. Try running with sudo.");
        toggleMonitoringButton->setText("Start");
        toggleMonitoringButton->setStyleSheet(
            "QPushButton {"
            "background-color: #2ecc71;"
            "color: white;"
            "border-radius: 6px;"
            "font-size: 20px;"
            "font-weight: bold;"
            "}"
            "QPushButton:pressed {"
            "background-color: #27ae60;"
            "}"
            );
        return;
    }

    std::cout << "Started " << device_count << " monitoring thread(s).\n";

    // Start timer
    start_time = std::chrono::steady_clock::now();
    updateTimer->start(1000); // Update UI every second
}

void MainWindow::stopMonitoring()
{
    std::cout << "=== Stopping Monitor ===\n";

    // Set the flag to stop monitoring threads
    monitoring = false;
    std::cout << "Monitoring flag set to: " << monitoring.load() << "\n";

    // Stop update timer
    updateTimer->stop();

    // Join threads
    for (auto& t : monitorThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    monitorThreads.clear();

    // Final update and reset UI
    updateDashboard();
    elapsedTimeLabel->setText("00 : 00 : 00");

    std::cout << "All monitoring threads stopped.\n";
}

bool MainWindow::isKeyboardDevice(const QString& devicePath) {
    QFile file(devicePath);
    int fd = open(devicePath.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Failed to open device for keyboard check: " << devicePath.toStdString() << " - " << strerror(errno) << "\n";
        return false;
    }

    libevdev* dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        std::cerr << "libevdev init failed for keyboard check: " << devicePath.toStdString() << " - " << strerror(-rc) << "\n";
        ::close(fd);
        return false;
    }

    bool is_keyboard = libevdev_has_event_type(dev, EV_KEY) &&
                       (libevdev_has_event_code(dev, EV_KEY, KEY_A) ||
                        libevdev_has_event_code(dev, EV_KEY, KEY_SPACE) ||
                        libevdev_has_event_code(dev, EV_KEY, KEY_ENTER));

    libevdev_free(dev);
    ::close(fd);

    return is_keyboard;
}

bool MainWindow::isMouseDevice(const QString& devicePath) {
    QFile file(devicePath);
    int fd = open(devicePath.toStdString().c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Failed to open device for mouse check: " << devicePath.toStdString() << " - " << strerror(errno) << "\n";
        return false;
    }

    libevdev* dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        std::cerr << "libevdev init failed for mouse check: " << devicePath.toStdString() << " - " << strerror(-rc) << "\n";
        ::close(fd);
        return false;
    }

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
    ::close(fd);

    bool is_mouse = has_mouse_buttons || has_scroll || has_motion;

    return is_mouse;
}

void MainWindow::monitorDevice(const std::string& path, DeviceType type) {
    std::cout << "[thread] Attempting to open: " << path << "\n";

    int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "[thread] Failed to open " << path << ": " << strerror(errno) << "\n";
        return;
    }

    libevdev* dev = nullptr;
    int rc = libevdev_new_from_fd(fd, &dev);
    if (rc < 0) {
        std::cerr << "[thread] libevdev init failed for " << path << ": " << strerror(-rc) << "\n";
        ::close(fd);
        return;
    }

    const char* name = libevdev_get_name(dev) ? libevdev_get_name(dev) : "(unknown)";
    std::cout << "[thread] Successfully monitoring " << path << " -> " << name << "\n";

    while (monitoring.load()) {
        input_event ev;
        rc = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (rc == LIBEVDEV_READ_STATUS_SUCCESS) {
            if (type == DeviceType::Keyboard && ev.type == EV_KEY && ev.value == 1) {
                keyboard_count.fetch_add(1);
                std::cout << "[keyboard] Key press detected on " << name << "\n";
            }
            if (type == DeviceType::Mouse && ev.type == EV_KEY && ev.value == 1) {
                if (ev.code == BTN_LEFT || ev.code == BTN_RIGHT || ev.code == BTN_MIDDLE) {
                    mouse_count.fetch_add(1);
                    std::cout << "[mouse] Button click detected on " << name << "\n";
                }
            }
            if (type == DeviceType::Mouse && ev.type == EV_REL) {
                if (ev.code == REL_WHEEL || ev.code == REL_HWHEEL) {
                    scroll_count.fetch_add(std::abs(ev.value));
                    std::cout << "[mouse] Scroll detected on " << name << ": " << ev.value << "\n";
                } else if (ev.code == REL_X) {
                    std::lock_guard<std::mutex> lock(mouse_distance_mutex);
                    mouse_distance += std::abs(static_cast<double>(ev.value));
                    std::cout << "[mouse] Movement X detected on " << name << ": " << ev.value << "\n";
                } else if (ev.code == REL_Y) {
                    std::lock_guard<std::mutex> lock(mouse_distance_mutex);
                    mouse_distance += std::abs(static_cast<double>(ev.value));
                    std::cout << "[mouse] Movement Y detected on " << name << ": " << ev.value << "\n";
                }
            }
        } else if (rc == -EAGAIN) {
            usleep(SLEEP_MICROSECONDS);
        } else if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            continue;
        } else if (rc != LIBEVDEV_READ_STATUS_SUCCESS) {
            std::cerr << "[thread] Read error on " << path << ": " << strerror(-rc) << "\n";
            break;
        }
    }

    std::cout << "[thread] Exiting monitor for " << name << "\n";
    libevdev_free(dev);
    ::close(fd);
}
