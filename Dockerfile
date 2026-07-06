FROM ubuntu:22.04

# Prevent interactive configuration screens during installation
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies required to compile the codebase
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    make \
    && rm -rf /var/lib/apt/lists/*

# Set up project directory inside the container
WORKDIR /app

# Copy the local codebase into the container
COPY . .

# Build the project in Release mode
RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --config Release

# Install the executable into /usr/local/bin so it is in the PATH
RUN cp build/minitop /usr/local/bin/minitop

# Run the process monitor by default
ENTRYPOINT ["minitop"]
