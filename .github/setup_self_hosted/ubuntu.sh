#!/bin/bash

# Should be run as root

apt -y update && apt -y upgrade
apt -y install docker.io

usermod -aG docker $USER
