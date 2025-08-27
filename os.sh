#!/bin/bash

# Check if libevdev-dev is installed
if ! dpkg -l | grep -q libevdev-dev; then
    echo "Installing libevdev-dev..."
    sudo apt-get update
    sudo apt-get install -y libevdev-dev
fi

# Compile the program
echo "Compiling the program..."
gcc -o os os.c -levdev -lX11 -I/usr/include/libevdev-1.0
sudo ./os
