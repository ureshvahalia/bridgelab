# Use an official Ubuntu base image
FROM ubuntu:24.04

# Install required packages
RUN apt-get update && apt-get install -y \
    build-essential \
    make \
    libboost-all-dev \
    libomp-dev \
    flex bison	\
    && rm -rf /var/lib/apt/lists/*

# Set the working directory inside the container
WORKDIR /app

# Copy project files into the container
COPY . /app

# Build your project
# If you're using CMake:
RUN make

# If you're using a single g++ command, comment the above and use:
# RUN g++ -fopenmp main.cpp helper.cpp -o my_program -lboost_system

# Set default command
ENTRYPOINT ["./wslbin/Debug/bidder"]
