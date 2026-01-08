FROM golang:1.22-alpine AS build

WORKDIR /src

COPY go.mod go.sum ./
RUN go mod download

COPY . .

# Build a small static binary.
RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -ldflags="-s -w" -o /out/cloudtunnel ./main.go

FROM gcr.io/distroless/static:nonroot

ENV LISTEN_ADDR=":8080"

COPY --from=build /out/cloudtunnel /cloudtunnel

EXPOSE 8080

USER nonroot:nonroot

ENTRYPOINT ["/cloudtunnel"]

