# Build the image next to the Hoist binary for your architecture, for example BasaltHoist-26.3.1-Linux-x64.
FROM debian:stable-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
COPY BasaltHoist-*-Linux-x64 /usr/local/bin/hoist
WORKDIR /srv/basalt
VOLUME /srv/basalt
EXPOSE 25565
# Hoist installs the product on the first start. Run with `docker run --stop-timeout 120` so the world can be saved.
ENTRYPOINT ["hoist", "run", "--target", "server"]
