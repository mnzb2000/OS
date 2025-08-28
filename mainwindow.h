#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <chrono>
#include <string>

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onToggleMonitoring();
    void updateDashboard();

private:
    // UI Elements
    QLabel *keyboardCountLabel;
    QLabel *mouseCountLabel;
    QLabel *scrollCountLabel;
    QLabel *mouseDistanceLabel;
    QLabel *elapsedTimeLabel;
    QPushButton *toggleMonitoringButton;
    QTimer *updateTimer;

    // Monitoring state
    std::atomic<bool> monitoring{false};

    // Monitoring threads
    std::vector<std::thread> monitorThreads;

    // Shared data
    std::atomic<int> keyboard_count{0};
    std::atomic<int> mouse_count{0};
    std::atomic<int> scroll_count{0};
    double mouse_distance = 0.0;
    std::mutex mouse_distance_mutex;
    std::chrono::steady_clock::time_point start_time;

    enum class DeviceType {
        Keyboard,
        Mouse
    };

    // Monitoring functions
    void startMonitoring();
    void stopMonitoring();
    bool isKeyboardDevice(const QString& devicePath);
    bool isMouseDevice(const QString& devicePath);
    void monitorDevice(const std::string& path, DeviceType type);
};

#endif // MAINWINDOW_H
