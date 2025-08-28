#!/bin/bash

# Check if libevdev-dev is installed
if ! dpkg -l | grep -q libevdev-dev; then
    echo "Installing libevdev-dev..."
    sudo apt-get update
    sudo apt-get install -y libevdev-dev
fi

# Compile the C++ program
echo "Compiling the program..."
g++ -std=c++17 z.cpp -o z $(pkg-config --cflags --libs libevdev)

# Run the program with sudo
sudo ./z
