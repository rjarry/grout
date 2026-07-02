// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

package grout

import (
	"context"
	"encoding/binary"
	"io"
	"net"
	"syscall"
)

type Event struct {
	Type    uint32
	Payload any
}

type eventDecoder func([]byte) (any, error)

var eventDecoders = map[uint32]eventDecoder{}

func registerEventDecoder(evType uint32, dec eventDecoder) {
	eventDecoders[evType] = dec
}

func (c *Client) Subscribe(ctx context.Context, sockPath string, types ...uint32) (<-chan Event, error) {
	if sockPath == "" {
		sockPath = DefaultSockPath
	}

	conn, err := net.Dial("unix", sockPath)
	if err != nil {
		return nil, &Error{Errno: syscall.ECONNREFUSED, Op: "subscribe"}
	}

	evClient := &Client{conn: conn}
	if err := evClient.hello(); err != nil {
		_ = conn.Close()
		return nil, err
	}

	for _, t := range types {
		var req [8]byte
		putBool(req[:], 0, false)
		putU32(req[:], 4, t)
		if err := evClient.sendOnly(grEventSubscribe, req[:]); err != nil {
			_ = conn.Close()
			return nil, err
		}
	}

	ch := make(chan Event, 64)

	go func() {
		defer func() { _ = conn.Close() }()
		defer close(ch)

		for {
			select {
			case <-ctx.Done():
				return
			default:
			}

			var hdr [eventHeaderSize]byte
			if _, err := io.ReadFull(conn, hdr[:]); err != nil {
				return
			}

			evType := binary.NativeEndian.Uint32(hdr[0:4])
			payloadLen := binary.NativeEndian.Uint64(hdr[8:16])

			if payloadLen > ApiMaxMsgLen {
				return
			}

			var data []byte
			if payloadLen > 0 {
				data = make([]byte, payloadLen)
				if _, err := io.ReadFull(conn, data); err != nil {
					return
				}
			}

			ev := Event{Type: evType}
			if dec, ok := eventDecoders[evType]; ok && data != nil {
				if payload, err := dec(data); err == nil {
					ev.Payload = payload
				}
			}

			select {
			case ch <- ev:
			case <-ctx.Done():
				return
			}
		}
	}()

	return ch, nil
}
