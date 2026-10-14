# Two images in one file:
#   docker build --target exechub -t tradebot-exechub .
#   docker build --target tradebot -t tradebot .
FROM golang:1.25-bookworm AS gobuild
WORKDIR /src
COPY services/exechub/ ./
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/exechub .

FROM gcr.io/distroless/static-debian12:nonroot AS exechub
COPY --from=gobuild /out/exechub /exechub
WORKDIR /data
EXPOSE 50051 8080
ENTRYPOINT ["/exechub", "-grpc", "0.0.0.0:50051", "-http", "0.0.0.0:8080", "-journal", "/data/exechub.jsonl"]

FROM python:3.12-slim AS tradebot
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 PIP_NO_CACHE_DIR=1
RUN useradd --create-home --uid 10001 trader
WORKDIR /app
COPY pyproject.toml README.md ./
COPY src ./src
RUN pip install --only-binary=:all: grpcio==1.66.2 && pip install ".[all]"
COPY prompts ./prompts
# All state (runs/, config/, releases/, KILL files) lives in the mounted volume.
WORKDIR /state
RUN chown trader:trader /state
USER trader
ENTRYPOINT ["tradebot"]
CMD ["status"]
