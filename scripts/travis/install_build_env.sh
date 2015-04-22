#!/bin/bash
# Script called by Travis to install the build environment for CUDArrays. This script must be called with sudo.

set -e

MAKE="make --jobs=$NUM_THREADS"

# Install apt packages where the Ubuntu 12.04 default and ppa works for Caffe

# This ppa is for gcc-4.9
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get -y update
apt-get install \
    gcc-4.9

wget http://www.cmake.org/files/v3.1/cmake-3.1.3-Linux-x86_64.sh
sh cmake-3.1.3-Linux-x86_64.sh --skip-license --prefix=/usr/local/cmake
find . -type d -exec chmod a+rx {} \;
chmod -R a+r software/cmake
chmod -R a+x software/cmake/bin
chmod -R a+x software/cmake/share

# Install CUDA
CUDA_URL=http://developer.download.nvidia.com/compute/cuda/7_0/Prod/local_installers/rpmdeb/cuda-repo-ubuntu1204-7-0-local_7.0-28_amd64.deb
CUDA_FILE=/tmp/cuda_install.deb
curl $CUDA_URL -o $CUDA_FILE
dpkg -i $CUDA_FILE
rm -f $CUDA_FILE
apt-get -y update
# Install the minimal CUDA subpackages required to test Caffe build.
# For a full CUDA installation, add 'cuda' to the list of packages.
apt-get -y install cuda-toolkit-7
# Create CUDA symlink at /usr/local/cuda
# (This would normally be created by the CUDA installer, but we create it
# manually since we did a partial installation.)
ln -s /usr/local/cuda-7 /usr/local/cuda