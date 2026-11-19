#!/usr/bin/env python3
"""Minimal FlatBuffers image server for checking the ambient light sender.

Accepts the Register and RawImage requests HyperHDR and Hyperion.ng understand,
answers like those servers, and reports each connection's frame rate. With
--snapshot the newest frame is written as a PPM image so its colors can be
compared with the stream.
"""
import argparse
import socket
import struct
import time
from pathlib import Path

COMMAND_IMAGE = 2
COMMAND_REGISTER = 4
IMAGE_TYPE_RAW = 1


class ProtocolError(Exception):
    pass


class Table:
    def __init__(self, data, position):
        self.data = data
        self.position = position
        self.vtable = position - self._i32(position)
        self.vtable_size = self._u16(self.vtable)
        if self.vtable < 0 or self.vtable + self.vtable_size > len(data):
            raise ProtocolError("vtable out of bounds")

    def _u16(self, position):
        return struct.unpack_from("<H", self.data, position)[0]

    def _i32(self, position):
        return struct.unpack_from("<i", self.data, position)[0]

    def field(self, field_id):
        entry = 4 + 2 * field_id
        if entry >= self.vtable_size:
            return None
        offset = self._u16(self.vtable + entry)
        return self.position + offset if offset else None

    def u8(self, field_id):
        position = self.field(field_id)
        return 0 if position is None else self.data[position]

    def i32(self, field_id, default=0):
        position = self.field(field_id)
        return default if position is None else self._i32(position)

    def table(self, field_id):
        position = self.field(field_id)
        if position is None:
            raise ProtocolError(f"missing table field {field_id}")
        return Table(self.data, position + self._i32(position))

    def vector(self, field_id):
        position = self.field(field_id)
        if position is None:
            raise ProtocolError(f"missing vector field {field_id}")
        start = position + self._i32(position)
        size = self._i32(start)
        if start + 4 + size > len(self.data):
            raise ProtocolError("vector out of bounds")
        return self.data[start + 4 : start + 4 + size]


def reply(error=None, registered=-1):
    """Build a Reply with the same fields the real servers fill in."""
    text = b"" if error is None else error.encode()
    string_size = 0 if error is None else 4 + len(text) + 1
    payload = bytearray(28 + string_size + (-string_size % 4))
    struct.pack_into("<I", payload, 0, 16)
    struct.pack_into("<HHHHH", payload, 4, 10, 12, 4 if error else 0, 0, 8)
    struct.pack_into("<i", payload, 16, 12)
    struct.pack_into("<i", payload, 24, registered)
    if error:
        struct.pack_into("<I", payload, 20, 8)
        struct.pack_into("<I", payload, 28, len(text))
        payload[32 : 32 + len(text)] = text
    return struct.pack(">I", len(payload)) + bytes(payload)


def read_exact(connection, size):
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ConnectionError("client closed the connection")
        data += chunk
    return bytes(data)


def write_snapshot(path, width, height, pixels):
    temporary = path.with_suffix(".tmp")
    temporary.write_bytes(f"P6\n{width} {height}\n255\n".encode() + pixels)
    temporary.replace(path)


def serve_client(connection, snapshot):
    registered = None
    frames = 0
    window_start = time.monotonic()
    while True:
        size = struct.unpack(">I", read_exact(connection, 4))[0]
        data = read_exact(connection, size)
        request = Table(data, struct.unpack_from("<I", data, 0)[0])
        command = request.u8(0)

        if command == COMMAND_REGISTER:
            body = request.table(1)
            registered = body.i32(1)
            origin = bytes(body.vector(0)).decode()
            print(f"registered origin={origin!r} priority={registered}", flush=True)
            connection.sendall(reply(registered=registered))
        elif command == COMMAND_IMAGE:
            image = request.table(1)
            if image.u8(0) != IMAGE_TYPE_RAW:
                connection.sendall(reply(error="Only RawImage is supported"))
                continue
            raw = image.table(1)
            width, height = raw.i32(1, -1), raw.i32(2, -1)
            pixels = raw.vector(0)
            if width <= 0 or height <= 0 or len(pixels) != width * height * 3:
                connection.sendall(reply(error="Size of image data does not match"))
                continue

            frames += 1
            if snapshot:
                write_snapshot(snapshot, width, height, pixels)
            connection.sendall(reply(registered=registered if registered is not None else -1))

            elapsed = time.monotonic() - window_start
            if elapsed >= 2:
                average = sum(pixels) / len(pixels)
                print(f"{frames / elapsed:5.1f} fps {width}x{height} mean level {average:5.1f}", flush=True)
                frames = 0
                window_start = time.monotonic()
        else:
            connection.sendall(reply(error=f"Unsupported command {command}"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=19400)
    parser.add_argument("--snapshot", type=Path, help="write the newest frame to this PPM file")
    arguments = parser.parse_args()

    with socket.create_server((arguments.bind, arguments.port)) as server:
        print(f"listening on {arguments.bind}:{arguments.port}", flush=True)
        while True:
            connection, peer = server.accept()
            print(f"client connected from {peer[0]}:{peer[1]}", flush=True)
            with connection:
                try:
                    serve_client(connection, arguments.snapshot)
                except (ConnectionError, ProtocolError, struct.error, IndexError) as error:
                    print(f"client finished: {error}", flush=True)


if __name__ == "__main__":
    main()
