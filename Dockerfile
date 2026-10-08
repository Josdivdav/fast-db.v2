# Multi-stage build for FastDB C++ (standalone in fastdb dir)
FROM gcc:14 AS builder

WORKDIR /app
COPY . .

RUN make clean && make -j$(nproc)

FROM debian:bookworm-slim

WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/fastdb_server /app/fastdb_server
COPY --from=builder /app/public /app/public

ENV PORT=5500
ENV FASTDB_PUBLIC_DIR=/app/public

EXPOSE 5500

CMD ["/app/fastdb_server", "--port", "5500", "--public", "/app/public"]
