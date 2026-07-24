FROM docker.io/library/ubuntu:latest

# Enable universe and update
RUN apt update && apt install -y software-properties-common 2>/dev/null; \
    apt update

# Install what we need
RUN DEBIAN_FRONTEND=noninteractive apt install -y --no-install-recommends \
    okular xvfb x11vnc fluxbox xterm \
    && apt clean

# Copy our custom poppler generator plugin
COPY okularGenerator_poppler.so /usr/lib/x86_64-linux-gnu/qt6/plugins/okular_generators/okularGenerator_poppler.so

# Setup
RUN mkdir -p /home/testuser

COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

EXPOSE 5900
CMD ["/entrypoint.sh"]
